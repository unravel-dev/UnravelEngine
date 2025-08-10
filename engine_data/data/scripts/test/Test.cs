using System;
using System.Runtime.CompilerServices;
namespace Ace
{
namespace Core
{
public static class Tests
{


    [MethodImpl(MethodImplOptions.InternalCall)]
    public static extern Guid m2n_test_uuid(Guid uid);
}

}

}