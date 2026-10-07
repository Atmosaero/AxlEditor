#pragma once

class EditorContext;

class IEditorTool
{
public:
    virtual ~IEditorTool() = default;
    // Initialize/Shutdown run on the editor thread. Repeated calls must be safe.
    // The context outlives initialized tools; release borrowed registrations
    // before owned UI, and shut tools down in reverse initialization order.
    virtual void Initialize(EditorContext& context) = 0;
    virtual void Shutdown() = 0;
};
