// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * Thin wrapper for VolumeManager. This should be an interface proxy to talk
 * to VolumeManager. This class also filters Drive related data/events if
 * driveEnabled is set to false.
 *
 * @constructor
 * @extends {cr.EventTarget}
 * @implements {VolumeManagerCommon.VolumeInfoProvider}
 *
 * @param {!AllowedPaths} allowedPaths Which paths are supported in the Files
 *     app dialog.
 * @param {Window=} opt_backgroundPage Window object of the background
 *     page. If this is specified, the class skips to get background page.
 *     TOOD(hirono): Let all clients of the class pass the background page and
 *     make the argument not optional.
 */
function VolumeManagerWrapper(allowedPaths, opt_backgroundPage) {
  cr.EventTarget.call(this);

  this.allowedPaths_ = allowedPaths;
  this.volumeInfoList = new cr.ui.ArrayDataModel([]);

  this.volumeManager_ = null;
  this.pendingTasks_ = [];
  this.onEventBound_ = this.onEvent_.bind(this);
  this.onVolumeInfoListUpdatedBound_ =
      this.onVolumeInfoListUpdated_.bind(this);

  this.disposed_ = false;

  // Start initialize the VolumeManager.
  var queue = new AsyncUtil.Queue();

  if (opt_backgroundPage) {
    this.backgroundPage_ = opt_backgroundPage;
  } else {
    queue.run(function(callNextStep) {
      chrome.runtime.getBackgroundPage(/** @type {function(Window=)} */(
          function(opt_backgroundPage) {
            this.backgroundPage_ = opt_backgroundPage;
            callNextStep();
          }.bind(this)));
    }.bind(this));
  }

  queue.run(function(callNextStep) {
    this.backgroundPage_.volumeManagerFactory.getInstance(
        function(volumeManager) {
          this.onReady_(volumeManager);
          callNextStep();
        }.bind(this));
  }.bind(this));
}

/**
 * Extends cr.EventTarget.
 */
VolumeManagerWrapper.prototype.__proto__ = cr.EventTarget.prototype;

/**
 * @param {VolumeManagerCommon.VolumeType} volumeType
 * @return {boolean}
 */
VolumeManagerWrapper.prototype.isAllowedVolume_ = function(volumeType) {
  if (this.allowedPaths_ === AllowedPaths.ANY_PATH)
    return true;

  if (this.allowedPaths_ === AllowedPaths.NATIVE_OR_DRIVE_PATH &&
      (VolumeManagerCommon.VolumeType.isNative(volumeType) ||
       volumeType == VolumeManagerCommon.VolumeType.DRIVE)) {
    return true;
  }

  if (this.allowedPaths_ === AllowedPaths.NATIVE_PATH &&
      VolumeManagerCommon.VolumeType.isNative(volumeType)) {
    return true;
  }

  return false;
};

/**
 * Called when the VolumeManager gets ready for post initialization.
 * @param {VolumeManager} volumeManager The initialized VolumeManager instance.
 * @private
 */
VolumeManagerWrapper.prototype.onReady_ = function(volumeManager) {
  if (this.disposed_)
    return;

  this.volumeManager_ = volumeManager;

  // Subscribe to VolumeManager.
  this.volumeManager_.addEventListener(
      'drive-connection-changed', this.onEventBound_);
  this.volumeManager_.addEventListener(
      'externally-unmounted', this.onEventBound_);

  // Dispatch 'drive-connection-changed' to listeners, since the return value of
  // VolumeManagerWrapper.getDriveConnectionState() can be changed by setting
  // this.volumeManager_.
  cr.dispatchSimpleEvent(this, 'drive-connection-changed');

  // Cache volumeInfoList.
  var volumeInfoList = [];
  for (var i = 0; i < this.volumeManager_.volumeInfoList.length; i++) {
    var volumeInfo = this.volumeManager_.volumeInfoList.item(i);
    // TODO(hidehiko): Filter mounted volumes located on Drive File System.
    if (!this.isAllowedVolume_(volumeInfo.volumeType))
      continue;
    volumeInfoList.push(volumeInfo);
  }
  this.volumeInfoList.splice.apply(
      this.volumeInfoList,
      [0, this.volumeInfoList.length].concat(volumeInfoList));

  // Subscribe to VolumeInfoList.
  // In VolumeInfoList, we only use 'splice' event.
  this.volumeManager_.volumeInfoList.addEventListener(
      'splice', this.onVolumeInfoListUpdatedBound_);

  // Run pending tasks.
  var pendingTasks = this.pendingTasks_;
  this.pendingTasks_ = null;
  for (var i = 0; i < pendingTasks.length; i++)
    pendingTasks[i]();
};

/**
 * Disposes the instance. After the invocation of this method, any other
 * method should not be called.
 */
VolumeManagerWrapper.prototype.dispose = function() {
  this.disposed_ = true;

  if (!this.volumeManager_)
    return;
  this.volumeManager_.removeEventListener(
      'drive-connection-changed', this.onEventBound_);
  this.volumeManager_.removeEventListener(
      'externally-unmounted', this.onEventBound_);
  this.volumeManager_.volumeInfoList.removeEventListener(
      'splice', this.onVolumeInfoListUpdatedBound_);
};

/**
 * Called on events sent from VolumeManager. This has responsibility to
 * re-dispatch the event to the listeners.
 * @param {!Event} event Event object sent from VolumeManager.
 * @private
 */
VolumeManagerWrapper.prototype.onEvent_ = function(event) {
  var eventVolumeType;
  switch (event.type) {
    case 'drive-connection-changed':
      eventVolumeType = VolumeManagerCommon.VolumeType.DRIVE;
      break;
    case 'externally-unmounted':
      event = /** @type {!ExternallyUnmountedEvent} */ (event);
      eventVolumeType = event.volumeInfo.volumeType;
      break;
  }

  if (eventVolumeType && this.isAllowedVolume_(eventVolumeType))
    this.dispatchEvent(event);
};

/**
 * Called on events of modifying VolumeInfoList.
 * @param {Event} event Event object sent from VolumeInfoList.
 * @private
 */
VolumeManagerWrapper.prototype.onVolumeInfoListUpdated_ = function(event) {
  if (this.allowedPaths_ === AllowedPaths.ANY_PATH) {
    // Apply the splice as is.
    this.volumeInfoList.splice.apply(
         this.volumeInfoList,
         [event.index, event.removed.length].concat(event.added));
    return;
  }

  // Filters Drive related volumes.
  var index = event.index;
  for (var i = 0; i < event.index; i++) {
    var volumeType = this.volumeManager_.volumeInfoList.item(i).volumeType;
    if (!this.isAllowedVolume_(volumeType))
      index--;
  }

  var numRemovedVolumes = 0;
  for (var i = 0; i < event.removed.length; i++) {
    if (this.isAllowedVolume_(event.removed[i].volumeType))
      numRemovedVolumes++;
  }

  var addedVolumes = [];
  for (var i = 0; i < event.added.length; i++) {
    var volumeInfo = event.added[i];
    if (this.isAllowedVolume_(volumeInfo.volumeType)) {
      addedVolumes.push(volumeInfo);
    }
  }

  this.volumeInfoList.splice.apply(
      this.volumeInfoList, [index, numRemovedVolumes].concat(addedVolumes));
};

/**
 * Returns whether the VolumeManager is initialized or not.
 * @return {boolean} True if the VolumeManager is initialized.
 */
VolumeManagerWrapper.prototype.isInitialized = function() {
  return this.pendingTasks_ === null;
};

/**
 * Ensures the VolumeManager is initialized, and then invokes callback.
 * If the VolumeManager is already initialized, callback will be called
 * immediately.
 * @param {function()} callback Called on initialization completion.
 */
VolumeManagerWrapper.prototype.ensureInitialized = function(callback) {
  if (!this.isInitialized()) {
    this.pendingTasks_.push(this.ensureInitialized.bind(this, callback));
    return;
  }

  callback();
};

/**
 * @return {VolumeManagerCommon.DriveConnectionState} Current drive connection
 *     state.
 */
VolumeManagerWrapper.prototype.getDriveConnectionState = function() {
  if (!this.isAllowedVolume_(VolumeManagerCommon.VolumeType.DRIVE) ||
      !this.volumeManager_) {
    return {
      type: VolumeManagerCommon.DriveConnectionType.OFFLINE,
      reason: VolumeManagerCommon.DriveConnectionReason.NO_SERVICE
    };
  }

  return this.volumeManager_.getDriveConnectionState();
};

/** @override */
VolumeManagerWrapper.prototype.getVolumeInfo = function(entry) {
  return this.filterDisabledVolume_(
      this.volumeManager_ && this.volumeManager_.getVolumeInfo(entry));
};

/**
 * Obtains a volume information of the current profile.
 * @param {VolumeManagerCommon.VolumeType} volumeType Volume type.
 * @return {VolumeInfo} Found volume info.
 */
VolumeManagerWrapper.prototype.getCurrentProfileVolumeInfo =
    function(volumeType) {
  return this.filterDisabledVolume_(
      this.volumeManager_ &&
      this.volumeManager_.getCurrentProfileVolumeInfo(volumeType));
};

/**
 * Obtains the default display root entry.
 * @param {function(Entry)} callback Callback passed the default display root.
 */
VolumeManagerWrapper.prototype.getDefaultDisplayRoot =
    function(callback) {
  this.ensureInitialized(function() {
    var defaultVolume = this.getCurrentProfileVolumeInfo(
        VolumeManagerCommon.VolumeType.DOWNLOADS);
    defaultVolume.resolveDisplayRoot(callback, function() {
      // defaultVolume is DOWNLOADS and resolveDisplayRoot should succeed.
      throw new Error(
          'Unexpectedly failed to obtain the default display root.');
    });
  }.bind(this));
};

/**
 * Obtains location information from an entry.
 *
 * @param {(!Entry|!FakeEntry)} entry File or directory entry.
 * @return {EntryLocation} Location information.
 */
VolumeManagerWrapper.prototype.getLocationInfo = function(entry) {
  var locationInfo =
      this.volumeManager_ && this.volumeManager_.getLocationInfo(entry);
  if (!locationInfo)
    return null;
  if (!this.filterDisabledVolume_(locationInfo.volumeInfo))
    return null;
  return locationInfo;
};

/**
 * Requests to mount the archive file.
 * @param {string} fileUrl The path to the archive file to be mounted.
 * @param {function(VolumeInfo)} successCallback Called with the VolumeInfo
 *     instance.
 * @param {function(VolumeManagerCommon.VolumeError)} errorCallback Called when
 *     an error occurs.
 */
VolumeManagerWrapper.prototype.mountArchive = function(
    fileUrl, successCallback, errorCallback) {
  if (this.pendingTasks_) {
    this.pendingTasks_.push(
        this.mountArchive.bind(this, fileUrl, successCallback, errorCallback));
    return;
  }

  this.volumeManager_.mountArchive(fileUrl, successCallback, errorCallback);
};

/**
 * Requests unmount the specified volume.
 * @param {!VolumeInfo} volumeInfo Volume to be unmounted.
 * @param {function()} successCallback Called on success.
 * @param {function(VolumeManagerCommon.VolumeError)} errorCallback Called when
 *     an error occurs.
 */
VolumeManagerWrapper.prototype.unmount = function(
    volumeInfo, successCallback, errorCallback) {
  if (this.pendingTasks_) {
    this.pendingTasks_.push(
        this.unmount.bind(this, volumeInfo, successCallback, errorCallback));
    return;
  }

  this.volumeManager_.unmount(volumeInfo, successCallback, errorCallback);
};

/**
 * Requests configuring of the specified volume.
 * @param {!VolumeInfo} volumeInfo Volume to be configured.
 * @return {!Promise} Fulfilled on success, otherwise rejected with an error
 *     message.
 */
VolumeManagerWrapper.prototype.configure = function(volumeInfo) {
  if (this.pendingTasks_) {
    return new Promise(function(fulfill, reject) {
      this.pendingTasks_.push(function() {
        return this.volumeManager_.configure(volumeInfo).then(fulfill, reject);
      }.bind(this));
    }.bind(this));
  }

  return this.volumeManager_.configure(volumeInfo);
};

/**
 * Filters volume info by referring allowedPaths_.
 *
 * @param {VolumeInfo} volumeInfo Volume info.
 * @return {VolumeInfo} Null if the volume is disabled. Otherwise just returns
 *     the volume.
 * @private
 */
VolumeManagerWrapper.prototype.filterDisabledVolume_ =
    function(volumeInfo) {
  if (volumeInfo && this.isAllowedVolume_(volumeInfo.volumeType)) {
    return volumeInfo;
  } else {
    return null;
  }
};

/**
 * Returns current state of VolumeManagerWrapper.
 * @return {string} Current state of VolumeManagerWrapper.
 */
VolumeManagerWrapper.prototype.toString = function() {
  var initialized = this.isInitialized();
  var volumeManager = initialized ?
      this.volumeManager_ :
      this.backgroundPage_.volumeManagerFactory.getInstanceForDebug();

  var str = 'VolumeManagerWrapper\n' +
      '- Initialized: ' + initialized + '\n';

  if (!initialized)
    str += '- PendingTasksCount: ' + this.pendingTasks_.length + '\n';

  return str + '- VolumeManager:\n' +
      '  ' + volumeManager.toString().replace(/\n/g, '\n  ');
};
