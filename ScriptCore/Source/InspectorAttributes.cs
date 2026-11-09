using System;

namespace Stardust
{
    // Expose a private field in the Inspector and persist its value in scenes/prefabs.
    [AttributeUsage(AttributeTargets.Field)]
    public sealed class SerializeField : Attribute { }

    [AttributeUsage(AttributeTargets.Field)]
    public sealed class HideInInspector : Attribute { }
}
