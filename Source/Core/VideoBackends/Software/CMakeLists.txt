add_library(videosoftware
  Clipper.cpp
  DebugUtil.cpp
  EfbCopy.cpp
  EfbInterface.cpp
  Rasterizer.cpp
  SWOGLWindow.cpp
  SWRenderer.cpp
  SWTexture.cpp
  SWVertexLoader.cpp
  SWmain.cpp
  SetupUnit.cpp
  Tev.cpp
  TextureEncoder.cpp
  TextureSampler.cpp
  TransformUnit.cpp
)

target_link_libraries(videosoftware
PUBLIC
  common
  videocommon

PRIVATE
  ${X11_LIBRARIES}
)
