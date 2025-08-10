using System;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Ace
{
namespace Core
{
    /// <summary>
    /// Represents a layer mask that can be used to include or exclude layers.
    /// </summary>
    [StructLayout(LayoutKind.Sequential)]
    public struct LayerMask
    {
        /// <summary>
        /// The raw integer value representing the layer mask.
        /// </summary>
        public int value;

        /// <summary>
        /// Converts the specified <see cref="LayerMask"/> to its integer representation.
        /// </summary>
        /// <param name="mask">The <see cref="LayerMask"/> to convert.</param>
        /// <returns>An <see cref="int"/> that represents the layer mask.</returns>
        public static implicit operator int(LayerMask mask)
        {
            return mask.value;
        }

        /// <summary>
        /// Converts the specified <see cref="int"/> to a <see cref="LayerMask"/>.
        /// </summary>
        /// <param name="intVal">The integer value that represents a layer mask.</param>
        /// <returns>A <see cref="LayerMask"/> constructed from the integer value.</returns>
        public static implicit operator LayerMask(int intVal)
        {
            LayerMask result = default(LayerMask);
            result.value = intVal;
            return result;
        }

        /// <summary>
        /// Adds the specified layer index to this mask.
        /// </summary>
        /// <param name="mask">The original layer mask.</param>
        /// <param name="layerIndex">The zero-based index of the layer to add.</param>
        /// <returns>A new <see cref="LayerMask"/> including the specified layer.</returns>
        public static LayerMask operator +(LayerMask mask, int layerIndex)
        {
            mask.value |= 1 << layerIndex;
            return mask;
        }

        /// <summary>
        /// Removes the specified layer index from this mask.
        /// </summary>
        /// <param name="mask">The original layer mask.</param>
        /// <param name="layerIndex">The zero-based index of the layer to remove.</param>
        /// <returns>A new <see cref="LayerMask"/> excluding the specified layer.</returns>
        public static LayerMask operator -(LayerMask mask, int layerIndex)
        {
            mask.value &= ~(1 << layerIndex);
            return mask;
        }

        /// <summary>
        /// Combines two layer masks (union).
        /// </summary>
        /// <param name="a">First layer mask.</param>
        /// <param name="b">Second layer mask.</param>
        /// <returns>A new <see cref="LayerMask"/> that is the union of the two masks.</returns>
        public static LayerMask operator +(LayerMask a, LayerMask b)
        {
            a.value |= b.value;
            return a;
        }

        /// <summary>
        /// Subtracts one layer mask from another (difference).
        /// </summary>
        /// <param name="a">Original layer mask.</param>
        /// <param name="b">Mask containing layers to remove from the original mask.</param>
        /// <returns>A new <see cref="LayerMask"/> with the layers in <paramref name="b"/> removed.</returns>
        public static LayerMask operator -(LayerMask a, LayerMask b)
        {
            a.value &= ~b.value;
            return a;
        }
        

        /// <summary>
        /// Given a layer index, returns the name of the layer as defined in either a built-in
        /// or a user-defined layer configuration.
        /// </summary>
        /// <param name="layer">The zero-based index of the layer.</param>
        /// <returns>The name of the layer, or <c>null</c> if the layer is not defined.</returns>
        public static string LayerToName(int layer)
        {
            return internal_m2n_layers_layer_to_name(layer);
        }

        /// <summary>
        /// Given a layer name, returns the corresponding layer index as defined in either a built-in
        /// or a user-defined layer configuration.
        /// </summary>
        /// <param name="layerName">The name of the layer.</param>
        /// <returns>The zero-based index of the layer, or <c>-1</c> if no layer with that name exists.</returns>
        public static int NameToLayer(string layerName)
        {
            return internal_m2n_layers_name_to_layer(layerName);
        }

        /// <summary>
        /// Given an array of layer names, returns a layer mask containing all the layers that match those names.
        /// </summary>
        /// <param name="layerNames">The names of the layers to include in the mask.</param>
        /// <returns>An integer representing the combined layer mask for all provided layers.</returns>
        /// <exception cref="ArgumentNullException">Thrown when <paramref name="layerNames"/> is <c>null</c>.</exception>
        public static int GetMask(params string[] layerNames)
        {
            if (layerNames == null)
            {
                throw new ArgumentNullException(nameof(layerNames));
            }

            int num = 0;
            foreach (string layerName in layerNames)
            {
                int layerIndex = NameToLayer(layerName);
                if (layerIndex != -1)
                {
                    num |= 1 << layerIndex;
                }
            }

            return num;
        }

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string internal_m2n_layers_layer_to_name(int layer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern int internal_m2n_layers_name_to_layer(string layerName);
    }

}
}

