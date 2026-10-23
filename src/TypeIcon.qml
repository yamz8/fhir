import QtQuick

// The mark for a resource type, in the timeline and the resource tree.
Image {
    property string type
    readonly property var names: ({
        "Encounter": "encounter",
        "Observation": "observation",
        "Condition": "condition",
        "MedicationRequest": "medication",
        "Procedure": "procedure",
        "Immunization": "immunization",
        "DiagnosticReport": "report",
        "AllergyIntolerance": "allergy",
        "Patient": "patient"
    })
    source: "qrc:/icons/" + (names[type] || "resource") + ".svg"
    sourceSize: Qt.size(18, 18)
    width: 18
    height: 18
    opacity: 0.85
}
