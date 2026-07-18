/*
 * Write DrawIndexedIndirect args from alive counter.
 * Layout: numIndices, numInstances, firstIndex, startVertex, startInstance
 */

#include <bgfx_compute.sh>

BUFFER_RO(s_counter, uint, 0);
BUFFER_WO(s_indirect, uint, 1);

uniform vec4 u_args0; // index_count, pad, pad, pad

NUM_THREADS(1, 1, 1)
void main()
{
    uint alive = s_counter[0];
    s_indirect[0] = uint(u_args0.x); // numIndices (6 for quad)
    s_indirect[1] = alive;           // numInstances
    s_indirect[2] = 0u;              // firstIndex
    s_indirect[3] = 0u;              // startVertex
    s_indirect[4] = 0u;              // startInstance
}
