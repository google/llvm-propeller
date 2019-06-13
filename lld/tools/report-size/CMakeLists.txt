set(LLVM_LINK_COMPONENTS
  Object
  Support
  )
add_lld_tool(report-size
  report-size.cpp
  PLOELFView.cpp
  )
  
target_link_libraries(report-size
  PRIVATE
  lldELF
  )
  
install(TARGETS report-size
	RUNTIME DESTINATION bin)
