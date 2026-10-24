# Resource layer (above core, beside the RHI): loads source assets into CPU structs.
# Third-party parsers are PRIVATE: their headers don't reach users of the library.
add_library(lmao_resource STATIC
    gltf_loader.h
    gltf_loader.cpp
    model.h
    third_party_impl.cpp # CGLTF_IMPLEMENTATION, STB_IMAGE_IMPLEMENTATION
)
add_library(Lmao::resource ALIAS lmao_resource)

target_link_libraries(lmao_resource
    PUBLIC Lmao::core
    PRIVATE cgltf stb)

lmao_set_warnings(lmao_resource)
set_target_properties(lmao_resource PROPERTIES FOLDER "engine")
