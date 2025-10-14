file(REMOVE_RECURSE
  "libkolibri_core.a"
  "libkolibri_core.pdb"
)

# Per-language clean rules from dependency scanning.
foreach(lang C)
  include(CMakeFiles/kolibri_core.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
