executable ('zynqmp_vcu_encode',
sources : ['test-vcu-encode.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep, gstvideo_dep]
)

executable ('zynqmp_sei_extraction',
sources : ['test-sei-extraction.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep]
)

executable('gst-n-launch-1.0', 'gstnlaunch.c', install: true, dependencies : [gst_dep])

executable ('zynqmp_relative_qp_insertion',
sources : ['zynqmp_relative_qp_insertion.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep, gstvideo_dep]
)
