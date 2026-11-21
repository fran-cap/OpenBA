// android/app/src/main/java/org/openba/game/NetGet.java (src/platform/common/platform_net.h)
//
// NOT RETAIL. The Java half of Platform::Net::HttpsGet on Android (ext.update_check,
// docs/extensions/handins/UPDATECHECK.md): one HTTPS GET through the platform's own
// HttpsURLConnection, so the system's TLS stack and trust store verify the server's certificate
// (the default, never replaced: no TrustManager, no HostnameVerifier here). The native half,
// src/platform/android/net_android.cpp, finds this class through the activity's class loader and
// calls get() from its own worker thread.
//
// What goes out: the request line, Host, the given User-Agent and what the stack adds itself
// (Accept-Encoding, Connection). No cookies (the process has no CookieHandler), no cache, no
// credentials (no Authenticator), no user interaction.
//
// The whole call is bounded: the request runs on a daemon thread of its own and get() waits for it
// at most timeoutMs. A request still running then (a name that does not resolve in time, a slow
// server) is abandoned: its thread ends by itself on its connect / read timeouts and the deadline
// check of its read loop, disconnects, and its answer is dropped.
package org.openba.game;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.URL;
import java.net.URLConnection;

import javax.net.ssl.HttpsURLConnection;

final class NetGet {
    private NetGet() {}

    // The request, on its own thread. Its fields are read by get() only under the object's lock,
    // after `done`.
    private static final class Request implements Runnable {
        private final String url;
        private final String userAgent;
        private final int timeoutMs;
        private final int maxBytes;
        private final long deadline;

        private String step = "start";
        private boolean done;
        private int status;
        private byte[] body;
        private String failure;

        Request(String url, String userAgent, int timeoutMs, int maxBytes) {
            this.url = url;
            this.userAgent = userAgent;
            this.timeoutMs = timeoutMs;
            this.maxBytes = maxBytes;
            this.deadline = System.nanoTime() + timeoutMs * 1000000L;
        }

        private synchronized void at(String name) {
            step = name;
        }

        @Override
        public void run() {
            HttpsURLConnection conn = null;
            int code = 0;
            byte[] bytes = null;
            String fail = null;
            try {
                at("open");
                final URLConnection opened = new URL(url).openConnection();
                if (!(opened instanceof HttpsURLConnection)) {
                    throw new IllegalStateException("not an HTTPS connection");
                }
                conn = (HttpsURLConnection) opened;
                conn.setConnectTimeout(timeoutMs);
                conn.setReadTimeout(timeoutMs);
                conn.setInstanceFollowRedirects(true);
                conn.setUseCaches(false);
                conn.setDoInput(true);
                conn.setRequestMethod("GET");
                conn.setRequestProperty("User-Agent", userAgent);
                at("connect");
                conn.connect();
                at("status");
                code = conn.getResponseCode();
                if (code <= 0) {
                    throw new IllegalStateException("no HTTP status");
                }
                at("read");
                bytes = readCapped(code >= 400 ? conn.getErrorStream() : conn.getInputStream());
            } catch (Exception e) {
                code = 0;
                bytes = null;
                fail = e.getClass().getName() + (e.getMessage() != null ? ": " + e.getMessage() : "");
            } finally {
                if (conn != null) {
                    conn.disconnect();
                }
            }
            synchronized (this) {
                status = code;
                body = bytes;
                failure = fail;
                done = true;
                notifyAll();
            }
        }

        // The first maxBytes bytes of the stream (an error status may have none: null), the rest
        // dropped; past the deadline the read stops, get() has given up on it by then.
        private byte[] readCapped(InputStream in) throws Exception {
            final ByteArrayOutputStream out = new ByteArrayOutputStream();
            if (in == null) {
                return out.toByteArray();
            }
            try {
                final byte[] chunk = new byte[16384];
                while (out.size() < maxBytes) {
                    if (System.nanoTime() - deadline > 0) {
                        throw new IllegalStateException("past the deadline");
                    }
                    final int n = in.read(chunk, 0, Math.min(chunk.length, maxBytes - out.size()));
                    if (n < 0) {
                        break;
                    }
                    out.write(chunk, 0, n);
                }
            } finally {
                in.close();
            }
            return out.toByteArray();
        }
    }

    // Called by net_android.cpp. Returns the body (possibly empty) with status[0] the HTTP status,
    // or null with status[0] 0 and failure[0] naming the step that failed and why.
    static byte[] get(String url, String userAgent, int timeoutMs, int maxBytes, int[] status,
                      String[] failure) {
        status[0] = 0;
        failure[0] = null;
        final int bound = Math.max(timeoutMs, 1);
        final Request request = new Request(url, userAgent, bound, Math.max(maxBytes, 0));
        final Thread thread = new Thread(request, "OpenBA-net");
        thread.setDaemon(true);
        try {
            thread.start();
        } catch (Throwable t) {
            failure[0] = "thread: " + t;
            return null;
        }
        final long end = System.nanoTime() + bound * 1000000L;
        synchronized (request) {
            while (!request.done) {
                final long left = (end - System.nanoTime()) / 1000000L;
                if (left <= 0) {
                    break;
                }
                try {
                    request.wait(left);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    break;
                }
            }
            if (!request.done) {
                failure[0] = request.step + ": timed out after " + bound + " ms";
                return null;
            }
            if (request.status == 0) {
                failure[0] = request.step + ": " + request.failure;
                return null;
            }
            status[0] = request.status;
            return request.body;
        }
    }
}
