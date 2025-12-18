# A Simple Ray Tracer, with a twist

This project implements a basic raytracer, capable of the normal features like, shadows, reflections, and refractions.
However, the extra features are what differentiates it, the program uses a BVH to accelerate the raytracing process and the program also
implements some features from path tracing by stratigically supersampling the image where the user looks.

## Building
mkdir build
cd build
cmake ..
cmake --build .
./Raytracer

## TODO
 - [ ] Path tracing for details
 - [ ] Textures
 - [ ] Raymarch shadows?
 - [ ] Specularity?
 - [ ] Better TLAS builder (optimize based on camera position)