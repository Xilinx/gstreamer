// This file was generated by the Gtk# code generator.
// Any changes made will be lost if regenerated.

namespace Gst {

	using System;
	using System.Runtime.InteropServices;

#region Autogenerated code
	[GLib.GType (typeof (Gst.IteratorResultGType))]
	public enum IteratorResult {

		Done = 0,
		Ok = 1,
		Resync = 2,
		Error = 3,
	}

	internal class IteratorResultGType {
		[DllImport ("gstreamer-1.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr gst_iterator_result_get_type ();

		public static GLib.GType GType {
			get {
				return new GLib.GType (gst_iterator_result_get_type ());
			}
		}
	}
#endregion
}