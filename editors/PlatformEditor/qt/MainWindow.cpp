// ============================================================================
// PlatformEditor/qt/MainWindow.cpp -- see MainWindow.h
// ============================================================================

#include "MainWindow.h"

#include "ManifestModel.h"
#include "PropertyPane.h"

#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>

#include <cstdlib>
#include <functional>
#include <set>

// QtCore defines `emit' as an empty keyword-macro, which mangles the core's
// Document::emit() call.  Q_EMIT is used for the few signal raises below.
#undef emit

namespace platedit {

namespace {

std::string readAll(const QString& path, QString& err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        err = f.errorString();
        return {};
    }
    QByteArray b = f.readAll();
    return std::string(b.constData(), static_cast<std::size_t>(b.size()));
}

// Required-interface registry (emulator presence checks; mirrors the webui
// mockup).  TODO(policy): move into platform_schema.json as a `required'
// annotation so this is data-driven like everything else.
const std::set<std::string>& requiredIicAddresses() {
    static const std::set<std::string> req = {"0x70", "0x72", "0xA2",
                                             "0x40", "0x42", "0x9e"};
    return req;
}

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Build a synthetic object member scalar quickly.
void addScalar(Node* obj, NodeKind kind, const char* key, const QString& text) {
    obj->insertChild(Node::make(kind, key, text.toStdString()));
}

} // namespace

MainWindow::MainWindow(const QString& schemaDir, QWidget* parent)
    : QMainWindow(parent), schemaDir_(schemaDir) {
    buildUi();
    buildMenus();
    buildManifestDock();
    loadPolicyAndCatalog();
    refreshTitle();
    refreshCrudActions();
    resize(1240, 800);
    const QString lastDir =
        QSettings().value(QStringLiteral("lastFolder")).toString();
    if (!lastDir.isEmpty() && QFileInfo(lastDir).isDir()) setFolder(lastDir);
}

MainWindow::~MainWindow() = default;

// ---------------------------------------------------------------- UI assembly

void MainWindow::buildUi() {
    model_ = new ManifestModel(this);
    tree_  = new QTreeView(this);
    tree_->setModel(model_);
    tree_->setHeaderHidden(true);
    tree_->setUniformRowHeights(true);

    props_ = new PropertyPane(this);

    issues_ = new QListWidget(this);
    issues_->setAlternatingRowColors(true);

    auto* top = new QSplitter(Qt::Horizontal, this);
    top->addWidget(tree_);
    top->addWidget(props_);
    top->setStretchFactor(0, 0);
    top->setStretchFactor(1, 1);
    top->setSizes({380, 820});

    auto* outer = new QSplitter(Qt::Vertical, this);
    outer->addWidget(top);
    outer->addWidget(issues_);
    outer->setStretchFactor(0, 1);
    outer->setSizes({600, 160});
    setCentralWidget(outer);

    QToolBar* tb = addToolBar(tr("Device"));
    tb->setMovable(false);
    tb->addAction(tr("Open..."), this, &MainWindow::openManifest);
    tb->addAction(tr("Save"), this, &MainWindow::save);
    tb->addSeparator();
    actAddIic_ = tb->addAction(tr("+ IIC"), this, &MainWindow::addIic);
    actAddPci_ = tb->addAction(tr("+ PCI"), this, &MainWindow::addPci);
    actAddBar_ = tb->addAction(tr("+ BAR"), this, &MainWindow::addBar);

    // "+ Device" carries a type menu (bus-appropriate entries are enabled in
    // refreshCrudActions).
    auto* devMenu = new QMenu(this);
    devMenu->addAction(tr("Virtual Disk"), this, [this] {
        addStorage(QStringLiteral("disk"), QStringLiteral("image"));
    });
    devMenu->addAction(tr("Virtual CD-ROM"), this, [this] {
        addStorage(QStringLiteral("cdrom"), QStringLiteral("iso"));
    });
    devMenu->addAction(tr("Virtual Tape"), this, [this] {
        addStorage(QStringLiteral("tape"), QStringLiteral("image"));
    });
    devMenu->addAction(tr("Physical CD-ROM (host)"), this, [this] {
        addStorage(QStringLiteral("cdrom"), QStringLiteral("host"));
    });
    actAddDev_ = tb->addAction(tr("+ Device"));
    actAddDev_->setMenu(devMenu);
    if (auto* btn = qobject_cast<QToolButton*>(tb->widgetForAction(actAddDev_)))
        btn->setPopupMode(QToolButton::InstantPopup);

    tb->addSeparator();
    actDup_ = tb->addAction(tr("Duplicate"), this, &MainWindow::duplicateSel);
    actDel_ = tb->addAction(tr("Delete"), this, &MainWindow::deleteSel);

    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MainWindow::onSelectionChanged);
    connect(props_, &PropertyPane::fieldEdited,
            this, &MainWindow::onFieldEdited);
    connect(issues_, &QListWidget::itemActivated,
            this, &MainWindow::onIssueActivated);
    connect(issues_, &QListWidget::itemClicked,
            this, &MainWindow::onIssueActivated);

    statusBar()->showMessage(
        tr("Open a manifest (or File > Open Folder... to browse a directory)"));
}

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("&Open..."), QKeySequence::Open,
                    this, &MainWindow::openManifest);
    file->addAction(tr("Open &Folder..."),
                    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O),
                    this, &MainWindow::openFolder);
    file->addAction(tr("&Reload"), QKeySequence::Refresh,
                    this, &MainWindow::reload);
    file->addSeparator();
    file->addAction(tr("&Save"), QKeySequence::Save, this, &MainWindow::save);
    file->addAction(tr("Save &As..."), QKeySequence::SaveAs,
                    this, &MainWindow::saveAs);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::quit);

    QMenu* tools = menuBar()->addMenu(tr("&Tools"));
    tools->addAction(tr("&Validate"), QKeySequence(Qt::CTRL | Qt::Key_L),
                     this, &MainWindow::validateNow);

    QMenu* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("&About"), this, &MainWindow::about);
}

void MainWindow::buildManifestDock() {
    fsModel_ = new QFileSystemModel(this);
    fsModel_->setNameFilters({QStringLiteral("*_platform.json"),
                              QStringLiteral("*.json")});
    fsModel_->setNameFilterDisables(false);
    fsView_ = new QListView(this);
    fsView_->setModel(fsModel_);
    fsView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(fsView_, &QListView::activated,
            this, &MainWindow::onManifestFileActivated);
    connect(fsView_, &QListView::clicked,
            this, &MainWindow::onManifestFileActivated);

    auto* dock = new QDockWidget(tr("Manifests"), this);
    dock->setObjectName(QStringLiteral("manifestsDock"));
    dock->setWidget(fsView_);
    dock->setFeatures(QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
}

void MainWindow::loadPolicyAndCatalog() {
    QString err;
    std::string perr, cerr2;
    const QString sp = schemaDir_ + "/schema/platform_schema.json";
    const QString cp = schemaDir_ + "/catalog/device_catalog.json";
    std::string s = readAll(sp, err);
    if (!s.empty()) policy_ = SchemaPolicy::load(s, perr);
    std::string c = readAll(cp, err);
    if (!c.empty()) catalog_ = DeviceCatalog::load(c, cerr2);
    if (policy_.empty())
        statusBar()->showMessage(tr("WARNING: policy not loaded from %1 %2")
                                     .arg(sp, QString::fromStdString(perr)));
    props_->setSources(&policy_, &catalog_);
}

// ------------------------------------------------------------- file handling

void MainWindow::setFolder(const QString& dir) {
    fsModel_->setRootPath(dir);
    fsView_->setRootIndex(fsModel_->index(dir));
    QSettings().setValue(QStringLiteral("lastFolder"), dir);
}

void MainWindow::openFolder() {
    QString d = QFileDialog::getExistingDirectory(
        this, tr("Open folder of platform manifests"),
        QSettings().value(QStringLiteral("lastFolder")).toString());
    if (!d.isEmpty()) setFolder(d);
}

void MainWindow::onManifestFileActivated(const QModelIndex& index) {
    const QString path = fsModel_->filePath(index);
    if (!QFileInfo(path).isFile() || path == filePath_) return;
    if (!confirmDiscard()) return;
    loadManifest(path);
}

bool MainWindow::confirmDiscard() {
    return !dirty_ ||
           QMessageBox::question(this, tr("Discard changes?"),
                                 tr("Unsaved edits will be lost. Continue?")) ==
               QMessageBox::Yes;
}

bool MainWindow::loadManifest(const QString& path, bool quiet) {
    QString ferr;
    std::string src = readAll(path, ferr);
    if (src.empty() && !ferr.isEmpty()) {
        if (!quiet)
            QMessageBox::critical(this, tr("Open failed"),
                                  tr("%1:\n%2").arg(path, ferr));
        return false;
    }
    ParseError perr;
    auto doc = std::make_unique<Document>(Document::parse(std::move(src), perr));
    if (!perr.ok || !doc->valid()) {
        if (!quiet)
            QMessageBox::critical(
                this, tr("Parse failed"),
                tr("%1:\n%2 (offset %3)")
                    .arg(path, QString::fromStdString(perr.message))
                    .arg(perr.offset));
        return false;
    }
    doc_      = std::move(doc);
    filePath_ = path;
    model_->setSources(doc_.get(), &policy_, &catalog_);
    tree_->expandAll();
    props_->clearTarget();
    setDirty(false);
    validateNow();
    refreshCrudActions();
    statusBar()->showMessage(tr("Loaded %1").arg(path), 5000);
    return true;
}

void MainWindow::openManifest() {
    if (!confirmDiscard()) return;
    QString f = QFileDialog::getOpenFileName(
        this, tr("Open platform manifest"),
        QSettings().value(QStringLiteral("lastFolder")).toString(),
        tr("Platform manifests (*_platform.json);;JSON (*.json);;All files (*)"));
    if (f.isEmpty()) return;
    QSettings().setValue(QStringLiteral("lastFolder"),
                         QFileInfo(f).absolutePath());
    loadManifest(f);
}

void MainWindow::reload() {
    if (filePath_.isEmpty() || !confirmDiscard()) return;
    loadManifest(filePath_);
}

bool MainWindow::writeTo(const QString& path) {
    if (!doc_) return false;
    if (QFile::exists(path)) {                       // .bak on overwrite
        const QString bak = path + ".bak";
        QFile::remove(bak);
        QFile::copy(path, bak);
    }
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(this, tr("Save failed"), f.errorString());
        return false;
    }
    const std::string out = doc_->emit();
    f.write(out.data(), static_cast<qint64>(out.size()));
    if (!f.commit()) {
        QMessageBox::critical(this, tr("Save failed"), f.errorString());
        return false;
    }
    setDirty(false);
    statusBar()->showMessage(tr("Saved %1 (backup: %1.bak)").arg(path), 5000);
    return true;
}

void MainWindow::save() {
    if (filePath_.isEmpty()) { saveAs(); return; }
    writeTo(filePath_);
}

void MainWindow::saveAs() {
    QString f = QFileDialog::getSaveFileName(
        this, tr("Save platform manifest"), filePath_,
        tr("Platform manifests (*_platform.json);;JSON (*.json)"));
    if (f.isEmpty()) return;
    if (writeTo(f)) { filePath_ = f; refreshTitle(); }
}

// ----------------------------------------------------------------- selection

Node* MainWindow::selectedNode() const {
    return model_->nodeFromIndex(tree_->currentIndex());
}

Node* MainWindow::selectedPci() const {
    Node* n = selectedNode();
    while (n && n->parent) {
        if (n->parent->kind == NodeKind::Array &&
            n->parent->key == "pci_devices")
            return n;
        n = n->parent;
    }
    return nullptr;
}

void MainWindow::onSelectionChanged() {
    props_->setTarget(selectedNode());
    refreshCrudActions();
}

void MainWindow::refreshCrudActions() {
    const bool haveDoc = doc_ && doc_->valid();
    Node* pci = haveDoc ? selectedPci() : nullptr;
    bool storageCtrl = false;
    if (pci) {
        // class_code 0x0101xx = IDE, 0x0100xx = SCSI-family mass storage.
        const Node* cc = pci->member("class_code");
        const std::string v = cc ? cc->text : std::string();
        storageCtrl = v.rfind("0x0101", 0) == 0 || v.rfind("0x0100", 0) == 0 ||
                      pci->member("storage") != nullptr;
    }
    Node* n = selectedNode();
    const bool isItem = n && n->parent && n->parent->kind == NodeKind::Array;
    actAddIic_->setEnabled(haveDoc);
    actAddPci_->setEnabled(haveDoc);
    actAddBar_->setEnabled(pci != nullptr);
    actAddDev_->setEnabled(storageCtrl);
    actAddDev_->setToolTip(storageCtrl
        ? tr("Add a disk/CD/tape to the selected controller")
        : tr("Select an IDE or SCSI storage controller first"));
    actDup_->setEnabled(isItem);
    actDel_->setEnabled(isItem);
}

// ------------------------------------------------------------ structural CRUD

void MainWindow::afterStructuralChange(Node* focus) {
    model_->setSources(doc_.get(), &policy_, &catalog_);   // reset
    tree_->expandAll();
    setDirty(true);
    validateNow();
    if (focus) {
        QModelIndex ix = model_->indexFromNode(focus);
        if (ix.isValid()) {
            tree_->setCurrentIndex(ix);
            tree_->scrollTo(ix);
        }
    }
    refreshCrudActions();
}

void MainWindow::addIic() {
    if (!doc_ || !doc_->valid()) return;
    Node* arr = doc_->root()->member("iic_devices");
    if (!arr) return;
    auto obj = Node::make(NodeKind::Object, "");
    addScalar(obj.get(), NodeKind::String, "name", QStringLiteral("new_iic"));
    addScalar(obj.get(), NodeKind::String, "address", QStringLiteral("0x00"));
    addScalar(obj.get(), NodeKind::String, "class", QStringLiteral("status"));
    addScalar(obj.get(), NodeKind::String, "byte", QStringLiteral("0x00"));
    afterStructuralChange(arr->insertChild(std::move(obj)));
}

void MainWindow::addPci() {
    if (!doc_ || !doc_->valid()) return;
    Node* arr = doc_->root()->member("pci_devices");
    if (!arr) return;
    auto obj = Node::make(NodeKind::Object, "");
    addScalar(obj.get(), NodeKind::String, "name", QStringLiteral("new_pci"));
    addScalar(obj.get(), NodeKind::String, "model", QStringLiteral("generic"));
    addScalar(obj.get(), NodeKind::Number, "hose", QStringLiteral("0"));
    addScalar(obj.get(), NodeKind::Number, "bus", QStringLiteral("0"));
    addScalar(obj.get(), NodeKind::Number, "slot", QStringLiteral("0"));
    addScalar(obj.get(), NodeKind::Number, "func", QStringLiteral("0"));
    addScalar(obj.get(), NodeKind::String, "vendor", QStringLiteral("0x0000"));
    addScalar(obj.get(), NodeKind::String, "device", QStringLiteral("0x0000"));
    addScalar(obj.get(), NodeKind::String, "class_code",
              QStringLiteral("0x000000"));
    addScalar(obj.get(), NodeKind::Bool, "option_rom", QStringLiteral("false"));
    addScalar(obj.get(), NodeKind::Number, "interrupt_pin", QStringLiteral("0"));
    afterStructuralChange(arr->insertChild(std::move(obj)));
}

void MainWindow::addBar() {
    Node* pci = selectedPci();
    if (!pci) return;
    Node* bars = pci->member("bars");
    if (!bars) bars = pci->insertChild(Node::make(NodeKind::Array, "bars"));
    auto b = Node::make(NodeKind::Object, "");
    addScalar(b.get(), NodeKind::Number, "index",
              QString::number(bars->children.size()));
    addScalar(b.get(), NodeKind::String, "kind", QStringLiteral("mem"));
    addScalar(b.get(), NodeKind::String, "size", QStringLiteral("0x0"));
    afterStructuralChange(bars->insertChild(std::move(b)));
}

void MainWindow::addStorage(const QString& type, const QString& mediaKind) {
    Node* pci = selectedPci();
    if (!pci) return;
    const Node* cc = pci->member("class_code");
    const bool scsi = cc && cc->text.rfind("0x0100", 0) == 0;
    if (!scsi && type == QLatin1String("tape")) {
        QMessageBox::information(this, tr("Not available"),
                                 tr("Tape is a SCSI device type; the selected "
                                    "controller is IDE."));
        return;
    }
    Node* st = pci->member("storage");
    if (!st) st = pci->insertChild(Node::make(NodeKind::Array, "storage"));

    // Next free (channel,unit): SCSI walks target ids skipping the initiator
    // (7 by default / channels[] sidecar); IDE walks ch0/1 x master/slave.
    long long initiator = 7;
    if (const Node* chArr = pci->member("channels"))
        if (const Node* ch0 = chArr->elem(0))
            if (const Node* init = ch0->member("initiator_id"))
                initiator = std::strtoll(init->text.c_str(), nullptr, 0);
    std::set<std::string> used;
    for (const auto& row : st->children) {
        const Node* c = row->member("channel");
        const Node* u = row->member("unit");
        used.insert((c ? c->text : "0") + "/" + (u ? u->text : "0"));
    }
    long long channel = 0, unit = 0;
    bool found = false;
    if (scsi) {
        for (long long u = 0; u < 16 && !found; ++u) {
            if (u == initiator) continue;
            if (!used.count("0/" + std::to_string(u))) {
                channel = 0; unit = u; found = true;
            }
        }
    } else {
        const long long cu[4][2] = {{0,0},{0,1},{1,0},{1,1}};
        for (auto& p : cu) {
            if (!used.count(std::to_string(p[0]) + "/" + std::to_string(p[1]))) {
                channel = p[0]; unit = p[1]; found = true; break;
            }
        }
    }
    if (!found) {
        QMessageBox::information(this, tr("Bus full"),
                                 tr("No free (channel, unit) address left on "
                                    "this controller."));
        return;
    }

    QString stype;
    if (type == QLatin1String("disk"))
        stype = scsi ? QStringLiteral("scsi_disk") : QStringLiteral("ata_disk");
    else if (type == QLatin1String("cdrom"))
        stype = scsi ? QStringLiteral("scsi_cdrom")
                     : QStringLiteral("atapi_cdrom");
    else
        stype = QStringLiteral("scsi_tape");

    auto row = Node::make(NodeKind::Object, "");
    addScalar(row.get(), NodeKind::Number, "channel", QString::number(channel));
    addScalar(row.get(), NodeKind::Number, "unit", QString::number(unit));
    addScalar(row.get(), NodeKind::Number, "lun", QStringLiteral("0"));
    addScalar(row.get(), NodeKind::String, "type", stype);
    addScalar(row.get(), NodeKind::String, "model",
              type == QLatin1String("cdrom")
                  ? QStringLiteral("EMULATR VIRTUAL CDROM")
                  : QStringLiteral("EMULATR VIRTUAL DISK"));
    addScalar(row.get(), NodeKind::String, "media", QString());
    addScalar(row.get(), NodeKind::String, "media_kind", mediaKind);
    afterStructuralChange(st->insertChild(std::move(row)));
}

void MainWindow::duplicateSel() {
    Node* n = selectedNode();
    if (!n || !n->parent || n->parent->kind != NodeKind::Array) return;
    Node* parent = n->parent;
    auto clone = cloneNode(*n);
    if (Node* nm = clone->member("name")) {
        nm->text  += "_copy";
        nm->dirty  = true;
        nm->edited = "\"" + nm->text + "\"";
    }
    const int at = parent->indexOfChild(n);
    afterStructuralChange(parent->insertChild(
        std::move(clone), at < 0 ? static_cast<std::size_t>(-1)
                                 : static_cast<std::size_t>(at) + 1));
}

void MainWindow::deleteSel() {
    Node* n = selectedNode();
    if (!n || !n->parent || n->parent->kind != NodeKind::Array) return;
    // Required-interface guard (emulator presence checks).
    if (n->parent->key == "iic_devices") {
        const Node* addr = n->member("address");
        if (addr && requiredIicAddresses().count(addr->text)) {
            QMessageBox::warning(
                this, tr("Required interface"),
                tr("IIC device at %1 is a required emulator interface and "
                   "cannot be deleted.").arg(qs(addr->text)));
            return;
        }
    }
    const Node* nm = n->member("name");
    const QString what = nm ? qs(nm->text) : qs(n->path());
    if (QMessageBox::question(this, tr("Delete"),
                              tr("Delete \"%1\"?").arg(what)) !=
        QMessageBox::Yes)
        return;
    Node* parent = n->parent;
    const int at = parent->indexOfChild(n);
    if (at < 0) return;
    parent->removeChildAt(static_cast<std::size_t>(at));
    props_->clearTarget();
    afterStructuralChange(parent);
}

// ------------------------------------------------------------------ validate

void MainWindow::validateNow() {
    findings_ = doc_ ? validateManifest(*doc_, &catalog_)
                     : std::vector<Finding>{};
    model_->setFindings(findings_);
    refreshIssues();
}

void MainWindow::refreshIssues() {
    issues_->clear();
    int err = 0, warn = 0;
    for (std::size_t i = 0; i < findings_.size(); ++i) {
        const Finding& f = findings_[i];
        if (f.sev == Severity::Err) ++err;
        if (f.sev == Severity::Warn) ++warn;
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  %2  %3")
                .arg(QString::fromLatin1(severityName(f.sev)).toUpper(), -5)
                .arg(QString::fromStdString(f.id))
                .arg(QString::fromStdString(f.message)),
            issues_);
        item->setData(Qt::UserRole, static_cast<qulonglong>(i));
        switch (f.sev) {
            case Severity::Err:  item->setForeground(QColor(0xf8, 0x51, 0x49)); break;
            case Severity::Warn: item->setForeground(QColor(0xd2, 0x99, 0x22)); break;
            case Severity::Info: item->setForeground(QColor(0x58, 0xa6, 0xff)); break;
        }
    }
    statusBar()->showMessage(tr("Validation: %1 issue(s) -- %2 error, %3 warn")
                                 .arg(findings_.size()).arg(err).arg(warn));
}

void MainWindow::onFieldEdited(const Node* scalar) {
    setDirty(true);
    model_->refreshNode(scalar);
    validateNow();
}

void MainWindow::onIssueActivated(QListWidgetItem* item) {
    const auto i = item->data(Qt::UserRole).toULongLong();
    if (i >= findings_.size() || !findings_[i].node) return;
    QModelIndex ix = model_->indexFromNode(findings_[i].node);
    if (!ix.isValid()) return;
    tree_->setCurrentIndex(ix);
    tree_->scrollTo(ix);
}

// -------------------------------------------------------------------- chrome

void MainWindow::setDirty(bool on) {
    dirty_ = on;
    refreshTitle();
}

void MainWindow::refreshTitle() {
    const QString name = filePath_.isEmpty()
                             ? tr("(no manifest)")
                             : QFileInfo(filePath_).fileName();
    setWindowTitle(tr("EmulatR Platform Editor - %1%2")
                       .arg(name, dirty_ ? QStringLiteral(" *") : QString()));
}

void MainWindow::about() {
    QMessageBox::about(
        this, tr("About EmulatR Platform Editor"),
        tr("<b>EmulatR</b> Platform Editor (Qt Widgets, D-025)<br>"
           "Authoring &amp; validation for EmulatR platform device manifests."
           "<br><br>Format-preserving core (SPEC-PLATED-001 / SPEC-SCSIH-001):"
           " scalar edits produce one-token diffs; structural edits regenerate"
           " only the touched container.<br><br>"
           "ASA-EmulatR -- Alpha AXP / EV6 Architecture Emulator<br>"
           "Copyright (C) 2025-2026 Timothy Peer (eNVy Systems, Inc.)"));
}

// ------------------------------------------------------------ selftest hooks

int MainWindow::treeRowCount() const {
    int count = 0;
    std::function<void(const QModelIndex&)> walk =
        [&](const QModelIndex& parent) {
            const int n = model_->rowCount(parent);
            count += n;
            for (int r = 0; r < n; ++r) walk(model_->index(r, 0, parent));
        };
    walk(QModelIndex());
    return count;
}

QStringList MainWindow::topLabels(int depth) const {
    QStringList out;
    std::function<void(const QModelIndex&, int)> walk =
        [&](const QModelIndex& parent, int d) {
            if (d >= depth) return;
            const int n = model_->rowCount(parent);
            for (int r = 0; r < n; ++r) {
                QModelIndex ix = model_->index(r, 0, parent);
                out << QString(d * 2, QLatin1Char(' ')) +
                           model_->data(ix, Qt::DisplayRole).toString();
                walk(ix, d + 1);
            }
        };
    walk(QModelIndex(), 0);
    return out;
}

} // namespace platedit
