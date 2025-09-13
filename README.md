# Sparse Matrix Multiplication using DirectX Work Graphs
This is an implementation of the algorithms described in "GPU's All Grown Up: Fully Device-Driven SpMV Using GPU Work Graphs" by Wildgrube et al.

The codebase is adapted from AMD's [Work Graph Playground](https://github.com/GPUOpen-LibrariesAndSDKs/WorkGraphPlayground). I have tried my best to annotate wherever I have modified the code (look for `//modified by` comments.) You should find most of the code relevent to SpMV in `src/SpMV_Application.cpp` and `./shaders`.

## References
Fabian Wildgrube, Pete Ehrett, Paul Trojahn, Richard Membarth, Bradford Beckmann, Dominik Baumeister, and Matthäus Chajdas. 2025. GPUs All Grown-Up: Fully Device-Driven SpMV Using GPU Work Graphs. In Proceedings of the 52nd Annual International Symposium on Computer Architecture (ISCA '25). Association for Computing Machinery, New York, NY, USA, 1777–1791. https://doi.org/10.1145/3695053.3731060