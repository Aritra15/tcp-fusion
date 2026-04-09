set(target_prefix scratch_assigned_experiments_)

build_exec(
  EXECNAME tcp-fusion-wifi-mobile
  EXECNAME_PREFIX ${target_prefix}
  SOURCE_FILES tcp-fusion-wifi-mobile.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/assigned_experiments/
)

build_exec(
  EXECNAME tcp-fusion-lrwpan-mobile
  EXECNAME_PREFIX ${target_prefix}
  SOURCE_FILES tcp-fusion-lrwpan-mobile.cc
  LIBRARIES_TO_LINK "${ns3-libs}" "${ns3-contrib-libs}"
  EXECUTABLE_DIRECTORY_PATH ${CMAKE_OUTPUT_DIRECTORY}/scratch/assigned_experiments/
)
