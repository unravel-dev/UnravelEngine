namespace Unravel.Core
{
    /// <summary>How a Line's or a Trail's strip is built around its points.</summary>
    public enum StripShape
    {
        /// <summary>A band of two vertices a point.</summary>
        Flat = 0,
        /// <summary>A band whose normals lean out at its edges, so it shades as a cylinder: cables, beams.</summary>
        Rounded = 1,
        /// <summary>A ring of vertices around each point, right from every side and in its shadow: ropes, pipes.</summary>
        Tube = 2,
    }

    /// <summary>What the face of a flat or rounded strip turns toward.</summary>
    public enum StripAlignment
    {
        /// <summary>The camera that draws it.</summary>
        Camera = 0,
        /// <summary>A direction that turns with the entity.</summary>
        LocalAxis = 1,
        /// <summary>A direction of the world, such as up for a path on the ground.</summary>
        WorldAxis = 2,
    }

    /// <summary>How a texture runs along a strip.</summary>
    public enum StripUVMode
    {
        /// <summary>Once from the strip's start to its end.</summary>
        Stretch = 0,
        /// <summary>One repeat every tile length, so it keeps its size as the strip grows.</summary>
        Tile = 1,
        /// <summary>One repeat between each pair of points.</summary>
        PerSegment = 2,
    }

    /// <summary>The whole-number settings of a strip, in the order the engine reads them.</summary>
    internal enum StripSetting
    {
        Shape = 0,
        Alignment = 1,
        UVMode = 2,
        Subdivisions = 3,
        CornerVertices = 4,
        CapVertices = 5,
        TubeSides = 6,
    }

    /// <summary>The numbers of a strip, in the order the engine reads them.</summary>
    internal enum StripNumber
    {
        Width = 0,
        TileLength = 1,
    }
}
