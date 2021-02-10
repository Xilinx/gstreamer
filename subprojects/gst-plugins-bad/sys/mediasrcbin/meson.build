mediasrcbin = get_option('mediasrcbin')
if mediasrcbin.disabled()
  have_mediasrcbin = false
  message('mediasrcbin plugin is disabled')
else
  have_v4l2 = cc.has_header('linux/videodev2.h') or cc.has_header('sys/videodev2.h') or cc.has_header('sys/videoio.h')
  if have_v4l2
    v4l2_dep = cc.find_library('v4l2subdev', required : false)
    have_v4l2 = v4l2_dep.found()
  endif
  have_mediactl = cc.has_header('mediactl/mediactl.h')
  if have_mediactl
    mediactl_dep = cc.find_library('mediactl', required : false)
    have_mediactl = mediactl_dep.found()
  endif
  if mediasrcbin.enabled() and have_v4l2 and have_mediactl
    have_mediasrcbin = true
  else
    error('mediasrcbin is requested but headers or libraries not found')
  endif
endif

if have_mediasrcbin
  gstmediasrcbin = library('gstmediasrcbin',
    'gstmediasrcbin.c', 'xmedialib.c',
    c_args: gst_plugins_bad_args + ['-I/usr/include/mediactl'],
    include_directories: [configinc],
    dependencies : [gst_dep, gstbase_dep, gstvideo_dep, v4l2_dep, mediactl_dep],
    install: true,
    install_dir: plugins_install_dir
  )
  pkgconfig.generate(gstmediasrcbin, install_dir: plugins_pkgconfig_install_dir)
endif
