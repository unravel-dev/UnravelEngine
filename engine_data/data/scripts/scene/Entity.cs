using System;
using System.Collections.Generic;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Ace
{
namespace Core
{
/// <summary>
/// Represents an entity within a scene. Provides methods to manage components and query entity properties.
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public struct Entity : IEquatable<Entity>, IFormattable
{
	/// <summary>
	/// Gets the unique identifier of the entity.
	/// </summary>
	public readonly uint Id;

	/// <summary>
	/// Gets or sets the wether the entity is active.
	/// </summary>
	/// <value>The active state associated with the entity.</value>
	public bool active
	{
		get => internal_m2n_get_active_global(this);
	}

	public bool activeLocal
	{
		get => internal_m2n_get_active_local(this);
		set => internal_m2n_set_active_local(this, value);
	}

	/// <summary>
	/// Gets or sets the name of the entity.
	/// </summary>
	/// <value>The name associated with the entity.</value>
	public string name
	{
		get => internal_m2n_get_name(this);
		set => internal_m2n_set_name(this, value);
	}

	/// <summary>
	/// Gets or sets the tag of the entity.
	/// </summary>
	/// <value>The tag associated with the entity.</value>
	public string tag
	{
		get => internal_m2n_get_tag(this);
		set => internal_m2n_set_tag(this, value);
	}

	/// <summary>
	/// Gets or sets the layer of the entity.
	/// </summary>
	/// <value>The layer associated with the entity.</value>
	public LayerMask layers
	{
		get => internal_m2n_get_layers(this);
		set => internal_m2n_set_layers(this, value);
	}

	/// <summary>
	/// Gets the transform component of the entity.
	/// </summary>
	/// <value>The transform component of the entity.</value>
	public TransformComponent transform
	{
		get => internal_m2n_get_transform_component(this, typeof(TransformComponent)) as TransformComponent;
	}

	/// <inheritdoc/>
	public override bool Equals(object obj)
	{
		if (obj == null || !(obj is Entity))
			return false;

		return Equals((Entity)obj);
	}

	/// <summary>
	/// Determines whether the specified entity is equal to the current entity.
	/// </summary>
	/// <param name="other">The entity to compare with the current entity.</param>
	/// <returns><c>true</c> if the specified entity is equal to the current entity; otherwise, <c>false</c>.</returns>
	public bool Equals(Entity other)
	{
		if (ReferenceEquals(this, other))
			return true;

		return Id == other.Id;
	}

	/// <summary>
	/// Compares two entities for equality.
	/// </summary>
	/// <param name="lhs">The first entity.</param>
	/// <param name="rhs">The second entity.</param>
	/// <returns><c>true</c> if both entities are equal; otherwise, <c>false</c>.</returns>
	public static bool operator ==(Entity lhs, Entity rhs) => lhs.Equals(rhs);

	/// <summary>
	/// Compares two entities for inequality.
	/// </summary>
	/// <param name="lhs">The first entity.</param>
	/// <param name="rhs">The second entity.</param>
	/// <returns><c>true</c> if both entities are not equal; otherwise, <c>false</c>.</returns>
	public static bool operator !=(Entity lhs, Entity rhs) => !(lhs == rhs);

	/// <inheritdoc/>
	public override int GetHashCode() => Id.GetHashCode();

	/// <summary>
	/// Determines whether the entity is valid within the scene.
	/// </summary>
	/// <returns><c>true</c> if the entity is valid; otherwise, <c>false</c>.</returns>
	public bool IsValid()
	{
		return Scene.IsEntityValid(this);
	}

	/// <summary>
	/// Adds a new component of the specified type to the entity.
	/// </summary>
	/// <typeparam name="T">The type of component to add.</typeparam>
	/// <returns>The newly added component.</returns>
	public T AddComponent<T>() where T : Component, new()
	{
		return internal_m2n_add_component(this, typeof(T)) as T;
	}

	/// <summary>
	/// Determines whether the entity has a component of the specified type.
	/// </summary>
	/// <typeparam name="T">The type of component to check for.</typeparam>
	/// <returns><c>true</c> if the entity has the specified component; otherwise, <c>false</c>.</returns>
	public bool HasComponent<T>() where T : Component
	{
		return internal_m2n_has_component(this, typeof(T));
	}

	/// <summary>
	/// Determines whether the entity has a component of the specified type.
	/// </summary>
	/// <param name="type">The type of component to check for.</param>
	/// <returns><c>true</c> if the entity has the specified component; otherwise, <c>false</c>.</returns>
	public bool HasComponent(Type type)
	{
		return internal_m2n_has_component(this, type);
	}

	/// <summary>
	/// Gets the component of the specified type from the entity.
	/// </summary>
	/// <typeparam name="T">The type of component to retrieve.</typeparam>
	/// <returns>The component of the specified type.</returns>
	public T GetComponent<T>() where T : Component, new()
	{
		return internal_m2n_get_component(this, typeof(T)) as T;
	}

	/// <summary>
	/// Gets all components of the specified type from the entity.
	/// </summary>
	/// <param name="type">The type of component to retrieve.</param>
	/// <returns>An array of components of the specified type.</returns>
	public Component[] GetComponents(Type type)
	{
		return (Component[])internal_m2n_get_components(this, type);
	}

	/// <summary>
	/// Gets all components of the specified type from the entity.
	/// </summary>
	/// <typeparam name="T">The type of component to retrieve.</typeparam>
	/// <returns>An array of components of the specified type.</returns>
	public T[] GetComponents<T>() where T : Component
	{
		return (T[])internal_m2n_get_components(this, typeof(T));
	}

	/// <summary>
/// Gets the first component of the specified type from the entity or any of its children.
/// </summary>
public T GetComponentInChildren<T>() where T : Component
{
    return internal_m2n_get_component_in_children(this, typeof(T)) as T;
}

/// <summary>
/// Gets all components of the specified type from the entity and its children.
/// </summary>
public T[] GetComponentsInChildren<T>() where T : Component
{
    return (T[])internal_m2n_get_components_in_children(this, typeof(T));
}

/// <summary>
/// Gets the first component of the specified type from the entity or any of its children.
/// </summary>
/// <param name="type">The component type to search for.</param>
public Component GetComponentInChildren(Type type)
{
    return internal_m2n_get_component_in_children(this, type);
}

/// <summary>
/// Gets all components of the specified type from the entity and its children.
/// </summary>
/// <param name="type">The component type to search for.</param>
public Component[] GetComponentsInChildren(Type type)
{
    return (Component[])internal_m2n_get_components_in_children(this, type);
}

	/// <summary>
	/// Removes a specified component instance from its owner.
	/// </summary>
	/// <param name="component">The component instance to remove.</param>
	/// <returns><c>true</c> if the component was successfully removed; otherwise, <c>false</c>.</returns>
	static public bool RemoveComponent(Component component)
	{
		return internal_m2n_remove_component_instance(component.owner, component);
	}

	/// <summary>
	/// Removes a specified component instance from its owner
	/// </summary>
	/// <param name="component">The component instance to remove.</param>
	/// <returns><c>true</c> if the component was successfully removed; otherwise, <c>false</c>.</returns>
	static public bool RemoveComponent(Component component, float secondsDelay)
	{
		return internal_m2n_remove_component_instance_delay(component.owner, component, secondsDelay);
	}

	/// <summary>
	/// Removes a component of the specified type from the entity.
	/// </summary>
	/// <typeparam name="T">The type of component to remove.</typeparam>
	/// <returns><c>true</c> if the component was successfully removed; otherwise, <c>false</c>.</returns>
	public bool RemoveComponent<T>() where T : Component
	{
		return internal_m2n_remove_component(this, typeof(T));
	}

	/// <summary>
	/// Removes a component of the specified type from the entity.
	/// </summary>
	/// <typeparam name="T">The type of component to remove.</typeparam>
	/// <returns><c>true</c> if the component was successfully removed; otherwise, <c>false</c>.</returns>
	public bool RemoveComponent<T>(float secondsDelay) where T : Component
	{
		return internal_m2n_remove_component_delay(this, typeof(T), secondsDelay);
	}

	/// <summary>
	/// Converts the collision to its string representation.
	/// </summary>
	/// <returns>A string that represents the collision.</returns>
	public override string ToString()
	{
		return ToString(null, null);
	}

	/// <summary>
	/// Converts the collision to its string representation with a specified format.
	/// </summary>
	/// <param name="format">The format string.</param>
	/// <returns>A string that represents the collision.</returns>
	public string ToString(string format)
	{
		return ToString(format, null);
	}

	/// <summary>
	/// Converts the collision to its string representation with a specified format and format provider.
	/// </summary>
	/// <param name="format">The format string.</param>
	/// <param name="formatProvider">An object that supplies culture-specific formatting information.</param>
	/// <returns>A string that represents the collision.</returns>
	public string ToString(string format, IFormatProvider formatProvider)
	{
		if (string.IsNullOrEmpty(format))
		{
			format = "F2";
		}

		if (formatProvider == null)
		{
			formatProvider = CultureInfo.InvariantCulture.NumberFormat;
		}

		return string.Format(formatProvider, "{0}", this.name);
	}

	/// <summary>
	/// Initializes a new instance of the <see cref="Entity"/> struct with the specified identifier.
	/// </summary>
	/// <param name="id">The unique identifier of the entity.</param>
	internal Entity(uint id)
	{
		Id = id;
	}

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern bool internal_m2n_get_active_global(Entity id);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern bool internal_m2n_get_active_local(Entity id);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern void internal_m2n_set_active_local(Entity id, bool active);


	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern string internal_m2n_get_name(Entity id);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern void internal_m2n_set_name(Entity id, string name);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern string internal_m2n_get_tag(Entity id);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern void internal_m2n_set_tag(Entity id, string tag);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern LayerMask internal_m2n_get_layers(Entity id);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern void internal_m2n_set_layers(Entity id, LayerMask layers);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern Component internal_m2n_add_component(Entity id, Type obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern Component internal_m2n_get_component(Entity id, Type obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern Array internal_m2n_get_components(Entity id, Type obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern Component internal_m2n_get_component_in_children(Entity id, Type obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern Array internal_m2n_get_components_in_children(Entity id, Type obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern bool internal_m2n_has_component(Entity id, Type obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern bool internal_m2n_remove_component_instance(Entity id, Component obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern bool internal_m2n_remove_component_instance_delay(Entity id, Component obj, float secondsDelay);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern bool internal_m2n_remove_component(Entity id, Type obj);

	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern bool internal_m2n_remove_component_delay(Entity id, Type obj, float secondsDelay);


	[MethodImpl(MethodImplOptions.InternalCall)]
	private static extern Component internal_m2n_get_transform_component(Entity id, Type obj);
}
}
}
