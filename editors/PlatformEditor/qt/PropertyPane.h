// ============================================================================
// PlatformEditor/qt/PropertyPane.h -- schema-driven property form
// ============================================================================
// Section 8 property pane over the shared core view-model: propertiesOf()
// supplies the scalar fields of the selected container in authored order with
// resolved tiers; this pane materializes one editor widget per tier
// (Section 6.3 table) and commits edits through EditOps::setScalar (the
// format-preserving dirty-scalar path).  All policy lives in the schema JSON;
// nothing here is per-field C++ (P-1).
// ============================================================================

#ifndef PLATEDIT_QT_PROPERTYPANE_H
#define PLATEDIT_QT_PROPERTYPANE_H

#include "core/DeviceCatalog.h"
#include "core/ManifestView.h"
#include "core/SchemaPolicy.h"

#include <QScrollArea>

class QFormLayout;
class QLabel;
class QWidget;

namespace platedit {

class PropertyPane final : public QScrollArea {
    Q_OBJECT
public:
    explicit PropertyPane(QWidget* parent = nullptr);

    void setSources(const SchemaPolicy* policy, const DeviceCatalog* catalog);
    void setTarget(Node* container);          // null clears the pane
    void clearTarget() { setTarget(nullptr); }

signals:
    // A scalar under the current container was successfully edited.
    void fieldEdited(const Node* scalar);

private:
    const SchemaPolicy*  policy_  = nullptr;
    const DeviceCatalog* catalog_ = nullptr;
    Node*                target_  = nullptr;

    QWidget*     body_  = nullptr;
    QLabel*      title_ = nullptr;
    QLabel*      sub_   = nullptr;
    QFormLayout* form_  = nullptr;

    void rebuild();
    QWidget* editorFor(const PropRow& row, const Rule* rule);
    void commit(Node* scalar, const QString& text, QWidget* editor);
};

} // namespace platedit

#endif // PLATEDIT_QT_PROPERTYPANE_H
