// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_PUBLIC_CHILD_REQUEST_PEER_H_
#define CONTENT_PUBLIC_CHILD_REQUEST_PEER_H_

#include <stdint.h>

#include <memory>
#include <string>

#include "content/common/content_export.h"

class GURL;

namespace base {
class TimeTicks;
}

namespace net {
struct RedirectInfo;
}

namespace content {

struct ResourceResponseInfo;

// This is implemented by our custom resource loader within content. The Peer
// and it's bridge should have identical lifetimes as they represent each end of
// a communication channel.
//
// These callbacks mirror net::URLRequest::Delegate and the order and
// conditions in which they will be called are identical. See url_request.h
// for more information.
class CONTENT_EXPORT RequestPeer {
 public:
  // This class represents data gotten from the Browser process. Each data
  // consists of |payload|, |length|, |encoded_data_length| and
  // |encoded_body_length|. The payload is valid only when the data instance is
  // valid.
  // In order to work with Chrome resource loading IPC, it is desirable to
  // reclaim data in FIFO order in a RequestPeer in terms of performance.
  // |payload|, |length|, |encoded_data_length| and |encoded_body_length|
  // functions are thread-safe, but the data object itself must be destroyed on
  // the original thread.
  class CONTENT_EXPORT ReceivedData {
   public:
    virtual ~ReceivedData() {}
    virtual const char* payload() const = 0;
    virtual int length() const = 0;
    // The encoded_data_length is the length of the encoded data transferred
    // over the network, including headers. It is only set for responses
    // originating from the network (ie. not the cache). It will usually be
    // different from length(), and may be smaller if the content was
    // compressed. -1 means this value is unavailable.
    virtual int encoded_data_length() const = 0;
    // The encoded_body_length is the size of the body as transferred over the
    // network or stored in the disk cache, excluding headers. This will be
    // different from length() if a content encoding was used.
    virtual int encoded_body_length() const = 0;
  };

  // A ThreadSafeReceivedData can be deleted on ANY thread.
  class CONTENT_EXPORT ThreadSafeReceivedData : public ReceivedData {};

  // Called as upload progress is made.
  // note: only for requests with upload progress enabled.
  virtual void OnUploadProgress(uint64_t position, uint64_t size) = 0;

  // Called when a redirect occurs.  The implementation may return false to
  // suppress the redirect.  The ResourceResponseInfo provides information about
  // the redirect response and the RedirectInfo includes information about the
  // request to be made if the method returns true.
  virtual bool OnReceivedRedirect(const net::RedirectInfo& redirect_info,
                                  const ResourceResponseInfo& info) = 0;

  // Called when response headers are available (after all redirects have
  // been followed).
  virtual void OnReceivedResponse(const ResourceResponseInfo& info) = 0;

  // Called when a chunk of response data is downloaded.  This method may be
  // called multiple times or not at all if an error occurs.  This method is
  // only called if RequestInfo::download_to_file was set to true, and in
  // that case, OnReceivedData will not be called.
  // The encoded_data_length is the length of the encoded data transferred
  // over the network, which could be different from data length (e.g. for
  // gzipped content).
  virtual void OnDownloadedData(int len, int encoded_data_length) = 0;

  // Called when a chunk of response data is available. This method may
  // be called multiple times or not at all if an error occurs.
  virtual void OnReceivedData(std::unique_ptr<ReceivedData> data) = 0;

  // Called when metadata generated by the renderer is retrieved from the
  // cache. This method may be called zero or one times.
  virtual void OnReceivedCachedMetadata(const char* data, int len) {}

  // Called when the response is complete.  This method signals completion of
  // the resource load.
  virtual void OnCompletedRequest(int error_code,
                                  bool was_ignored_by_handler,
                                  bool stale_copy_in_cache,
                                  const base::TimeTicks& completion_time,
                                  int64_t total_transfer_size) = 0;

  virtual ~RequestPeer() {}
};

}  // namespace content

#endif  // CONTENT_PUBLIC_CHILD_REQUEST_PEER_H_
