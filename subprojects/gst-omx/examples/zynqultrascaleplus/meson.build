gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'app_dep'])

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

executable ('zynqmp_appsrc_appsink_transcode',
sources : ['zynqmp_appsrc_appsink_transcode.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep, gstvideo_dep, gstallocators_dep, gstapp_dep]
)
