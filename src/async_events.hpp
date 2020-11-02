
/*
 *
 *  async_events.hpp
 *
 *  functions to handle asynchronous events for an RDMA device
 *
 */

#ifndef KNY_ASYNC_EVENTS_HPP_
#define KNY_ASYNC_EVENTS_HPP_

#include <cstdint>

#include <infiniband/verbs.h> 
#include <thread>

#include "error.hpp"

namespace kym {
  // Get's the next event 
  using AsyncEventHandler = Status (*)(struct ibv_context *ctx, struct ibv_async_event ev);

  // Gets events from given context and calls the given event handler
  Status RunAsyncEventHandler(struct ibv_context *ctx, AsyncEventHandler eh);

  // Prints the provided event to stderr
  Status LogAsyncEvent(struct ibv_context *ctx, struct ibv_async_event ev);

  std::thread TrailAsyncEvents(struct ibv_context *ctx);
}

#ifdef DEBUG
#define DebugTrailAsyncEvents(ctx) kym::TrailAsyncEvents(ctx)
#else
#define DebugTrailAsyncEvents(ctx) std::thread()
#endif

#endif // KNY_ASYNC_EVENTS_HPP
