#ifndef CLOUDMOON_ORBIS_CONNECTION_H
#define CLOUDMOON_ORBIS_CONNECTION_H

#include "cloudmoon_session_link.h"
#include "cloudmoon_client.h"

/* Runs the CloudMoon Socket.IO/WebSocket coordinator loop.
 * Call from a worker thread; the function blocks until the connection stops.
 */
int cm_orbis_cloudmoon_run_account(const cm_account_session_t *account,
                                   const char *ca_bundle_path,
                                   unsigned timeout_seconds,
                                   cm_cloudmoon_signal_fn on_signal,
                                   void *signal_context);

void cm_orbis_cloudmoon_request_stop(void);

#endif
