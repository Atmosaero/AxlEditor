#pragma once

class EditorContext;

class IEditorModule
{
public:
    virtual ~IEditorModule() = default;
    // Initialize/Shutdown run on the editor thread. Repeated calls must be safe.
    // The context outlives initialized modules; release borrowed registrations
    // before owned UI, and shut modules down in reverse initialization order.
    virtual void Initialize(EditorContext& context) = 0;
    virtual void Shutdown() = 0;
};
