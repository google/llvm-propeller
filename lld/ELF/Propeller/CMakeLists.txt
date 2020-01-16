find_package(Protobuf)
if( Protobuf_FOUND )
  add_compile_definitions(PROPELLER_PROTOBUF GOOGLE_PROTOBUF_NO_RTTI)
  include_directories( ${Protobuf_INCLUDE_DIRS} )
  protobuf_generate_cpp(PROPELLER_PROTO_SRCS PROPELLER_PROTO_HDRS propeller_cfg.proto)
  set(PROPELLER_PROTOBUF_LIBS ${Protobuf_LIBRARIES})
endif()

add_lld_library(lldPropeller
  Propeller.cpp
  PropellerBBReordering.cpp
  PropellerChainClustering.cpp
  PropellerCFG.cpp
  PropellerNodeChain.cpp
  PropellerNodeChainAssembly.cpp
  PropellerNodeChainBuilder.cpp
  PropellerProtobuf.cpp
  ${PROPELLER_PROTO_SRCS}

  LINK_LIBS
  ${PROPELLER_PROTOBUF_LIBS}
)
