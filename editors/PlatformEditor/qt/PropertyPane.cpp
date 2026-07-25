// ============================================================================
// PlatformEditor/qt/PropertyPane.cpp -- see PropertyPane.h
// ============================================================================

#include "PropertyPane.h"

#include "EditOps.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>
#include <limits>
#include <map>

namespace platedit {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }

// Focus-out commit filter for QPlainTextEdit (multiline prose fields).
class FocusCommit final : public QObject {
public:
    FocusCommit(QPlainTextEdit* edit, std::function<void()> fn)
        : QObject(edit), edit_(edit), fn_(std::move(fn)) {
        edit->installEventFilter(this);
    }
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == edit_ && ev->type() == QEvent::FocusOut) fn_();
        return QObject::eventFilter(obj, ev);
    }
private:
    QPlainTextEdit*       edit_;
    std::function<void()> fn_;
};
} // namespace

PropertyPane::PropertyPane(QWidget* parent) : QScrollArea(parent) {
    setWidgetResizable(true);
    body_ = new QWidget(this);
    auto* v = new QVBoxLayout(body_);
    v->setContentsMargins(18, 12, 18, 24);
    title_ = new QLabel(body_);
    QFont f = title_->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setBold(true);
    title_->setFont(f);
    sub_ = new QLabel(body_);
    sub_->setWordWrap(true);
    sub_->setStyleSheet("color: palette(mid);");
    form_ = new QFormLayout;
    form_->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    v->addWidget(title_);
    v->addWidget(sub_);
    v->addLayout(form_);
    v->addStretch(1);
    setWidget(body_);
    setTarget(nullptr);
}

void PropertyPane::setSources(const SchemaPolicy* policy,
                              const DeviceCatalog* catalog) {
    policy_  = policy;
    catalog_ = catalog;
    rebuild();
}

void PropertyPane::setTarget(Node* container) {
    target_ = container;
    rebuild();
}

void PropertyPane::rebuild() {
    while (form_->rowCount() > 0) form_->removeRow(0);
    if (!target_ || !policy_) {
        title_->setText(tr("No selection"));
        sub_->setText(tr("Select a node in the tree; its fields appear here."));
        return;
    }
    if (target_->kind == NodeKind::Array) {
        title_->setText(qs(target_->key));
        sub_->setText(tr("array of %1 -- select an item in the tree")
                          .arg(target_->children.size()));
        return;
    }

    const Node* nameNode = target_->member("name");
    title_->setText(nameNode ? qs(nameNode->text)
                             : (target_->parent ? qs(target_->key.empty()
                                    ? target_->path() : target_->key)
                                                : tr("platform root")));
    sub_->setText(qs(target_->path()));

    // Catalog drift map (V-07 presentation): field -> catalog default.
    std::map<std::string, std::string> drift;
    if (catalog_ && target_->member("model")) {
        for (const FieldConflict& fc : catalog_->conflictsForDevice(target_))
            drift[fc.field] = fc.catalog;
    }

    for (const PropRow& row : propertiesOf(target_, *policy_)) {
        const Rule* rule = policy_->resolve(row.node->path());
        QWidget* ed = editorFor(row, rule);

        auto* cell = new QWidget(body_);
        auto* h = new QHBoxLayout(cell);
        h->setContentsMargins(0, 0, 0, 0);
        h->addWidget(ed, 1);
        auto* tier = new QLabel(QStringLiteral("<small><i>%1</i></small>")
                                    .arg(tierName(row.tier)), cell);
        tier->setStyleSheet("color: palette(mid);");
        h->addWidget(tier);
        auto it = drift.find(row.key);
        if (it != drift.end()) {
            auto* badge = new QLabel(tr("<small>diverged (catalog %1)</small>")
                                         .arg(qs(it->second)), cell);
            badge->setStyleSheet("color: #d29922;");
            h->addWidget(badge);
        }
        form_->addRow(qs(row.key), cell);
    }
}

QWidget* PropertyPane::editorFor(const PropRow& row, const Rule* rule) {
    Node* scalar = row.node;
    const QString val = qs(row.value);
    const bool readOnly = rule && rule->readOnly;

    switch (row.tier) {
        case Tier::Int: {
            auto* w = new QSpinBox(body_);
            w->setRange(rule && rule->min ? static_cast<int>(*rule->min)
                                          : std::numeric_limits<int>::min(),
                        rule && rule->max ? static_cast<int>(*rule->max)
                                          : std::numeric_limits<int>::max());
            w->setValue(val.toInt());
            w->setEnabled(!readOnly);
            connect(w, &QAbstractSpinBox::editingFinished, this, [this, w, scalar] {
                commit(scalar, QString::number(w->value()), w);
            });
            return w;
        }
        case Tier::Bool: {
            auto* w = new QCheckBox(body_);
            w->setChecked(val == QLatin1String("true"));
            w->setEnabled(!readOnly);
            connect(w, &QCheckBox::toggled, this, [this, w, scalar](bool on) {
                commit(scalar, on ? QStringLiteral("true")
                                  : QStringLiteral("false"), w);
            });
            return w;
        }
        case Tier::Enum:
        case Tier::OpenEnum: {
            // openEnum with NO known values = free entry in practice (e.g.
            // device `name'): a plain line edit, not a one-item dropdown.
            if (row.tier == Tier::OpenEnum && (!rule || rule->values.empty())) {
                auto* w = new QLineEdit(val, body_);
                w->setReadOnly(readOnly);
                connect(w, &QLineEdit::editingFinished, this, [this, w, scalar] {
                    commit(scalar, w->text(), w);
                });
                return w;
            }
            auto* w = new QComboBox(body_);
            if (rule)
                for (const std::string& v : rule->values) w->addItem(qs(v));
            w->setEditable(row.tier == Tier::OpenEnum);
            if (w->findText(val) < 0) w->addItem(val);
            w->setCurrentText(val);
            w->setEnabled(!readOnly);
            connect(w, &QComboBox::activated, this, [this, w, scalar] {
                commit(scalar, w->currentText(), w);
            });
            if (row.tier == Tier::OpenEnum && w->lineEdit())
                connect(w->lineEdit(), &QLineEdit::editingFinished, this,
                        [this, w, scalar] { commit(scalar, w->currentText(), w); });
            return w;
        }
        case Tier::Multiline: {
            auto* w = new QPlainTextEdit(val, body_);
            w->setMinimumHeight(56);
            w->setMaximumHeight(120);
            w->setReadOnly(readOnly);
            new FocusCommit(w, [this, w, scalar] {
                commit(scalar, w->toPlainText(), w);
            });
            return w;
        }
        case Tier::Path: {
            auto* cell = new QWidget(body_);
            auto* h = new QHBoxLayout(cell);
            h->setContentsMargins(0, 0, 0, 0);
            auto* line = new QLineEdit(val, cell);
            auto* browse = new QPushButton(tr("Browse..."), cell);
            h->addWidget(line, 1);
            h->addWidget(browse);
            line->setReadOnly(readOnly);
            connect(line, &QLineEdit::editingFinished, this, [this, line, scalar] {
                commit(scalar, line->text(), line);
            });
            connect(browse, &QPushButton::clicked, this, [this, line, scalar] {
                QString f = QFileDialog::getOpenFileName(
                    this, tr("Select media"), QString(),
                    tr("Disk media (*.vdisk *.img *.iso);;All files (*)"));
                if (!f.isEmpty()) { line->setText(f); commit(scalar, f, line); }
            });
            return cell;
        }
        case Tier::Hex: {
            auto* w = new QLineEdit(val, body_);
            w->setValidator(new QRegularExpressionValidator(
                QRegularExpression(QStringLiteral("0[xX][0-9a-fA-F]+")), w));
            w->setReadOnly(readOnly);
            connect(w, &QLineEdit::editingFinished, this, [this, w, scalar] {
                commit(scalar, w->text(), w);
            });
            return w;
        }
        case Tier::Size:
        case Tier::Free:
        case Tier::Passthrough:
        default: {
            auto* w = new QLineEdit(val, body_);
            w->setReadOnly(readOnly);
            connect(w, &QLineEdit::editingFinished, this, [this, w, scalar] {
                commit(scalar, w->text(), w);
            });
            return w;
        }
    }
}

void PropertyPane::commit(Node* scalar, const QString& text, QWidget* editor) {
    if (!scalar) return;
    const std::string next = text.toStdString();
    if (next == scalar->text) return;                     // unchanged
    if (!setScalar(scalar, next)) {
        editor->setStyleSheet("border: 1px solid #f85149;");
        return;                                           // invalid literal
    }
    editor->setStyleSheet(QString());
    emit fieldEdited(scalar);
}

} // namespace platedit
