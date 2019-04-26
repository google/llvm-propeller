set(LLVM_LINK_COMPONENTS
  Support
  )

add_lld_tool(layout
  layout.cpp
  )

target_link_libraries(layout
  PRIVATE
  lldELF
  )

install(TARGETS layout
	RUNTIME DESTINATION bin)
