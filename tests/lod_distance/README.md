# Native geometry LOD distance

This asset-free suite executes the production comparison helper with synthetic
linear distances, squared distances and projected size ratios. It checks all six
scales, exact identity for the default and invalid settings (including unusual
float bit patterns), cull boundaries, and preservation of host floating-point
rounding, exception state and MXCSR. Generator fixtures reject changed, missing,
duplicated or misplaced instruction boundaries across the six native routines.

Configure this directory with CMake, build Release or Debug, then run CTest.
`XBOXRECOMP_DIR` can select the runtime sources containing `pc_video.h`.

The hooks scale only mesh-selection and prop-visibility comparisons. Physical
distances, sort keys, native submesh masks, index/count bounds, camera FOV,
frustum tests, streaming and texture mip levels retain their native behavior.
These tests do not establish additional distant content beyond what the current
level has loaded or prove performance at high scales.

Private acceptance additionally executes complete locally generated native
selectors against synthetic guest state. Those generated bodies stay outside
the repository. `RECOMP_BLACK_LOD_TRACE=1` prints bounded selected mesh LOD bytes
and physical/native/scaled distances for a matched game route; logs remain local.
