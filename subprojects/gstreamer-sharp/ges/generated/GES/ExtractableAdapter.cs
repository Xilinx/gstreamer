// This file was generated by the Gtk# code generator.
// Any changes made will be lost if regenerated.

namespace GES {

	using System;
	using System.Runtime.InteropServices;

#region Autogenerated code
	public partial class ExtractableAdapter : GLib.GInterfaceAdapter, GES.IExtractable {

		[StructLayout (LayoutKind.Sequential)]
		struct GESExtractableInterface {
			public GLib.GType AssetType;
			private GESSharp.ExtractableCheckIdNative _check_id;
			public GES.ExtractableCheckId CheckId {
				get {
					return GESSharp.ExtractableCheckIdWrapper.GetManagedDelegate (_check_id);
				}
			}
			public bool CanUpdateAsset;
			public SetAssetNativeDelegate SetAsset;
			IntPtr SetAssetFull;
			IntPtr GetParametersFromId;
			public GetIdNativeDelegate GetId;
			IntPtr GetRealExtractableType;
			IntPtr RegisterMetas;
			[MarshalAs (UnmanagedType.ByValArray, SizeConst=4)]
			public IntPtr[] _gesGesReserved;
		}

		static GESExtractableInterface iface;

		static ExtractableAdapter ()
		{
			GLib.GType.Register (_gtype, typeof (ExtractableAdapter));
			iface.SetAsset = new SetAssetNativeDelegate (SetAsset_cb);
			iface.GetId = new GetIdNativeDelegate (GetId_cb);
		}

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate void SetAssetNativeDelegate (IntPtr inst, IntPtr asset);

		static void SetAsset_cb (IntPtr inst, IntPtr asset)
		{
			try {
				IExtractableImplementor __obj = GLib.Object.GetObject (inst, false) as IExtractableImplementor;
				__obj.Asset = GLib.Object.GetObject(asset) as GES.Asset;
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, false);
			}
		}

		[UnmanagedFunctionPointer (CallingConvention.Cdecl)]
		delegate IntPtr GetIdNativeDelegate (IntPtr inst);

		static IntPtr GetId_cb (IntPtr inst)
		{
			try {
				IExtractableImplementor __obj = GLib.Object.GetObject (inst, false) as IExtractableImplementor;
				string __result;
				__result = __obj.Id;
				return GLib.Marshaller.StringToPtrGStrdup(__result);
			} catch (Exception e) {
				GLib.ExceptionManager.RaiseUnhandledException (e, true);
				// NOTREACHED: above call does not return.
				throw e;
			}
		}

		static int class_offset = 2 * IntPtr.Size;

		static void Initialize (IntPtr ptr, IntPtr data)
		{
			IntPtr ifaceptr = new IntPtr (ptr.ToInt64 () + class_offset);
			GESExtractableInterface native_iface = (GESExtractableInterface) Marshal.PtrToStructure (ifaceptr, typeof (GESExtractableInterface));
			native_iface.SetAsset = iface.SetAsset;
			native_iface.GetId = iface.GetId;
			Marshal.StructureToPtr (native_iface, ifaceptr, false);
		}

		GLib.Object implementor;

		public ExtractableAdapter ()
		{
			InitHandler = new GLib.GInterfaceInitHandler (Initialize);
		}

		public ExtractableAdapter (IExtractableImplementor implementor)
		{
			if (implementor == null)
				throw new ArgumentNullException ("implementor");
			else if (!(implementor is GLib.Object))
				throw new ArgumentException ("implementor must be a subclass of GLib.Object");
			this.implementor = implementor as GLib.Object;
		}

		public ExtractableAdapter (IntPtr handle)
		{
			if (!_gtype.IsInstance (handle))
				throw new ArgumentException ("The gobject doesn't implement the GInterface of this adapter", "handle");
			implementor = GLib.Object.GetObject (handle);
		}

		[DllImport("ges-1.0", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr ges_extractable_get_type();

		private static GLib.GType _gtype = new GLib.GType (ges_extractable_get_type ());

		public static GLib.GType GType {
			get {
				return _gtype;
			}
		}

		public override GLib.GType GInterfaceGType {
			get {
				return _gtype;
			}
		}

		public override IntPtr Handle {
			get {
				return implementor.Handle;
			}
		}

		public IntPtr OwnedHandle {
			get {
				return implementor.OwnedHandle;
			}
		}

		public static IExtractable GetObject (IntPtr handle, bool owned)
		{
			GLib.Object obj = GLib.Object.GetObject (handle, owned);
			return GetObject (obj);
		}

		public static IExtractable GetObject (GLib.Object obj)
		{
			if (obj == null)
				return null;
			else if (obj is IExtractableImplementor)
				return new ExtractableAdapter (obj as IExtractableImplementor);
			else if (obj as IExtractable == null)
				return new ExtractableAdapter (obj.Handle);
			else
				return obj as IExtractable;
		}

		public IExtractableImplementor Implementor {
			get {
				return implementor as IExtractableImplementor;
			}
		}

		[DllImport("ges-1.0", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr ges_extractable_get_asset(IntPtr raw);

		public GES.Asset Asset { 
			get {
				IntPtr raw_ret = ges_extractable_get_asset(Handle);
				GES.Asset ret = GLib.Object.GetObject(raw_ret) as GES.Asset;
				return ret;
			}
		}

		[DllImport("ges-1.0", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr ges_extractable_get_id(IntPtr raw);

		public string Id { 
			get {
				IntPtr raw_ret = ges_extractable_get_id(Handle);
				string ret = GLib.Marshaller.PtrToStringGFree(raw_ret);
				return ret;
			}
		}

		[DllImport("ges-1.0", CallingConvention = CallingConvention.Cdecl)]
		static extern bool ges_extractable_set_asset(IntPtr raw, IntPtr asset);

		public bool SetAsset(GES.Asset asset) {
			bool raw_ret = ges_extractable_set_asset(Handle, asset == null ? IntPtr.Zero : asset.Handle);
			bool ret = raw_ret;
			return ret;
		}

#endregion
	}
}