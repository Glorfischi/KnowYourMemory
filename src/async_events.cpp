#include <cstdint>

#include <infiniband/verbs.h> 
#include <thread>

#include <debug.h>
#include "error.hpp"

#include "async_events.hpp"

namespace kym {

  // Gets events from given context and calls the given event handler
  Status RunAsyncEventHandler(struct ibv_context *ctx, AsyncEventHandler eh){
    while(true){
      struct ibv_async_event ev;
      int ret = ibv_get_async_event(ctx, &ev);
      if (ret) {
        return Status(StatusCode::Internal, "Error  ibv_get_async_event failed");
      }
      auto stat = eh(ctx, ev);
      if (!stat.ok()){
        return stat;
      }
      ibv_ack_async_event(&ev);
    }
    return Status();
  }

  // Prints the provided event to stderr
  Status LogAsyncEvent(struct ibv_context *ctx, struct ibv_async_event ev){
    switch (ev.event_type) {
    /* QP events */
    case IBV_EVENT_QP_FATAL:
      info_wtime(stderr, "[async_event]QP fatal event for QP with handle %p\n", ev.element.qp);
      break;
    case IBV_EVENT_QP_REQ_ERR:
      info_wtime(stderr, "[async_event]QP Requestor error for QP with handle %p\n", ev.element.qp);
      break;
    case IBV_EVENT_QP_ACCESS_ERR:
      info_wtime(stderr, "[async_event]QP access error event for QP with handle %p\n", ev.element.qp);
      break;
    case IBV_EVENT_COMM_EST:
      info_wtime(stderr, "[async_event]QP communication established event for QP with handle %p\n", ev.element.qp);
      break;
    case IBV_EVENT_SQ_DRAINED:
      info_wtime(stderr, "[async_event]QP Send Queue drained event for QP with handle %p\n", ev.element.qp);
      break;
    case IBV_EVENT_PATH_MIG:
      info_wtime(stderr, "[async_event]QP Path migration loaded event for QP with handle %p\n", ev.element.qp);
      break;
    case IBV_EVENT_PATH_MIG_ERR:
      info_wtime(stderr, "[async_event]QP Path migration error event for QP with handle %p\n", ev.element.qp);
      break;
    case IBV_EVENT_QP_LAST_WQE_REACHED:
      info_wtime(stderr, "[async_event]QP last WQE reached event for QP with handle %p\n", ev.element.qp);
      break;
   
    /* CQ events */
    case IBV_EVENT_CQ_ERR:
      info_wtime(stderr, "[async_event]CQ error for CQ with handle %p\n", ev.element.cq);
      break;
   
    /* SRQ events */
    case IBV_EVENT_SRQ_ERR:
      info_wtime(stderr, "[async_event]SRQ error for SRQ with handle %p\n", ev.element.srq);
      break;
    case IBV_EVENT_SRQ_LIMIT_REACHED:
      info_wtime(stderr, "[async_event]SRQ limit reached event for SRQ with handle %p\n", ev.element.srq);
      break;
   
    /* Port events */
    case IBV_EVENT_PORT_ACTIVE:
      info_wtime(stderr, "[async_event]Port active event for port number %d\n", ev.element.port_num);
      break;
    case IBV_EVENT_PORT_ERR:
      info_wtime(stderr, "[async_event]Port error event for port number %d\n", ev.element.port_num);
      break;
    case IBV_EVENT_LID_CHANGE:
      info_wtime(stderr, "[async_event]LID change event for port number %d\n", ev.element.port_num);
      break;
    case IBV_EVENT_PKEY_CHANGE:
      info_wtime(stderr, "[async_event]P_Key table change event for port number %d\n", ev.element.port_num);
      break;
    case IBV_EVENT_GID_CHANGE:
      info_wtime(stderr, "[async_event]GID table change event for port number %d\n", ev.element.port_num);
      break;
    case IBV_EVENT_SM_CHANGE:
      info_wtime(stderr, "[async_event]SM change event for port number %d\n", ev.element.port_num);
      break;
    case IBV_EVENT_CLIENT_REREGISTER:
      info_wtime(stderr, "[async_event]Client reregister event for port number %d\n", ev.element.port_num);
      break;
   
    /* RDMA device events */
    case IBV_EVENT_DEVICE_FATAL:
      info_wtime(stderr, "[async_event]Fatal error event for device %s\n", ibv_get_device_name(ctx->device));
      break;
   
    default:
      info_wtime(stderr, "[async_event]Unknown event (%d)\n", ev.event_type);
    }
    return Status();
  }

  std::thread TrailAsyncEvents(struct ibv_context *ctx){
    std::thread async_thread([ctx](){
        RunAsyncEventHandler(ctx, LogAsyncEvent);
    });
    return async_thread;
  }
}

