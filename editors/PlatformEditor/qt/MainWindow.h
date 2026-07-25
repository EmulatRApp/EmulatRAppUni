// ============================================================================
// PlatformEditor/qt/MainWindow.h -- Qt Widgets shell (D-025 shipping frontend)
// ============================================================================
// Master-detail layout per Section 8 / the webui mockup: manifests dock +
// tree (left) | property pane (right), issues strip (bottom).  Owns the
// Document + policy + catalog; everything below this class is the shared
// Qt-free core.
//
// Structural CRUD (T-08) is live: add/duplicate/delete of IIC devices, PCI
// controllers, BARs and storage rows goes through Node::insertChild /
// removeChildAt / cloneNode; emit() regenerates only the touched container,
// so untouched sections keep their authored bytes.
// ============================================================================

#ifndef PLATEDIT_QT_MAINWINDOW_H
#define PLATEDIT_QT_MAINWINDOW_H

#include "core/DeviceCatalog.h"
#include "core/OrderedJson.h"
#include "core/SchemaPolicy.h"
#include "core/Validate.h"

#include <QMainWindow>

#include <memory>
#include <string>
#include <vector>

class QFileSystemModel;
class QListView;
class QListWidget;
class QListWidgetItem;
class QToolBar;
class QTreeView;

namespace platedit {

class ManifestModel;
class PropertyPane;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const QString& schemaDir, QWidget* parent = nullptr);
    ~MainWindow() override;

    bool loadManifest(const QString& path, bool quiet = false);

    // Headless acceptance hooks (--selftest).
    int  treeRowCount() const;
    int  findingCount() const { return static_cast<int>(findings_.size()); }
    QStringList topLabels(int depth = 2) const;

private slots:
    void openManifest();
    void openFolder();
    void onManifestFileActivated(const QModelIndex& index);
    void reload();
    void save();
    void saveAs();
    void validateNow();
    void onSelectionChanged();
    void onFieldEdited(const Node* scalar);
    void onIssueActivated(QListWidgetItem* item);
    void about();

    // structural CRUD
    void addIic();
    void addPci();
    void addBar();
    void addStorage(const QString& type, const QString& mediaKind);
    void duplicateSel();
    void deleteSel();

private:
    QString        schemaDir_;
    QString        filePath_;
    bool           dirty_ = false;

    std::unique_ptr<Document> doc_;
    SchemaPolicy              policy_;
    DeviceCatalog             catalog_;
    std::vector<Finding>      findings_;

    ManifestModel*    model_    = nullptr;
    QTreeView*        tree_     = nullptr;
    PropertyPane*     props_    = nullptr;
    QListWidget*      issues_   = nullptr;
    QFileSystemModel* fsModel_  = nullptr;
    QListView*        fsView_   = nullptr;

    QAction* actAddIic_ = nullptr;
    QAction* actAddPci_ = nullptr;
    QAction* actAddBar_ = nullptr;
    QAction* actAddDev_ = nullptr;
    QAction* actDup_    = nullptr;
    QAction* actDel_    = nullptr;

    void buildUi();
    void buildMenus();
    void buildManifestDock();
    void loadPolicyAndCatalog();
    void setFolder(const QString& dir);
    void setDirty(bool on);
    void refreshTitle();
    void refreshIssues();
    void refreshCrudActions();
    bool writeTo(const QString& path);

    Node* selectedNode() const;
    Node* selectedPci() const;           // selected node's pci_devices[*] ancestor
    // After a structural mutation: reset model, re-expand, select `focus'.
    void afterStructuralChange(Node* focus);
    bool confirmDiscard();
};

} // namespace platedit

#endif // PLATEDIT_QT_MAINWINDOW_H
