export enum ContentScriptMessageType {
  POINTER_LOCK = "POINTER_LOCK",
  GEOLOCATION_REQUEST = "GEOLOCATION_REQUEST",
  GEOLOCATION_RESPONSE = "GEOLOCATION_RESPONSE",
  LANGUAGE_UPDATE = "LANGUAGE_UPDATE",
}

export enum NativeHostMessageType {
  DOWNLOAD_COMPLETE = "DOWNLOAD_COMPLETE",
  NATIVE_HOST_EXIT = "NATIVE_HOST_EXIT",
  NATIVE_HOST_UPDATE = "NATIVE_HOST_UPDATE",
  POINTER_LOCK = "POINTER_LOCK",
  KEYBOARD_REPEAT_RATE = "KEYBOARD_REPEAT_RATE",
  TIMEZONE = "TIMEZONE",
}

export interface ContentScriptMessage {
  type: ContentScriptMessageType
  value: any
}

export interface NativeHostMessage {
  type: NativeHostMessageType
  value: any
}
