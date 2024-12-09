gst_req = '>= @0@.@1@.0'.format(gst_version_major, gst_version_minor)
gstapp_dep = dependency('gstreamer-app-1.0', version : gst_req,
  fallback : ['gst-plugins-base', 'app_dep'])

executable ('vcu2-gst-encode',
sources : ['vcu2_gst_encode.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep, gstvideo_dep]
)

executable ('vcu2-sei-extraction',
sources : ['vcu2_sei_extraction.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep]
)

executable('gst-n-launch-1.0', 'gstnlaunch.c', install: true, dependencies : [gst_dep])

executable ('vcu2-relative-qp-insertion',
sources : ['vcu2_relative_qp_insertion.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep, gstvideo_dep]
)

executable ('vcu2-appsrc-appsink-transcode',
sources : ['vcu2_appsrc_appsink_transcode.c'],
install: true,
c_args : gst_omx_args,
include_directories : [configinc],
dependencies : [gst_dep, gstvideo_dep, gstallocators_dep, gstapp_dep]
)
