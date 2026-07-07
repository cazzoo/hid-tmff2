//! Axum HTTP server — serves the dashboard and streams FF model state.
//!
//! Endpoints:
//!   GET /ws                → WebSocket (~60 Hz JSON push)
//!   GET /api/devices       → list FFB-capable evdev devices
//!   GET /api/gains         → probed per-force-type gains
//!   POST /api/gains        → live-write a gain value
//!   GET /                  → serve static web dashboard

use std::net::SocketAddr;
use std::sync::Arc;

use axum::{
    extract::State,
    http::StatusCode,
    response::{IntoResponse, Response},
    routing::get,
    Json, Router,
};
use axum::extract::ws::{WebSocket, WebSocketUpgrade, Message};
use futures_util::{SinkExt, StreamExt};
use tokio::net::TcpListener;
use tower_http::services::ServeDir;
use tracing::info;

use crate::proxy::{ProxyBackend, ProxyMode};

// ---------------------------------------------------------------------------
// App state
// ---------------------------------------------------------------------------

#[derive(Clone)]
pub struct AppState {
    pub backend: Arc<ProxyBackend>,
}

pub async fn serve(bind: &str, backend: Arc<ProxyBackend>) -> anyhow::Result<()> {
    let state = AppState { backend };

    let web_dir =
        std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("web");

    let api = Router::new()
        .route("/devices", get(list_devices))
        .route("/gains", get(gains_get).post(gains_post));

    let ws_route = Router::new()
        .route("/ws", get(ws_handler));

    let app = Router::new()
        .nest("/api", api)
        .merge(ws_route)
        .fallback_service(
            ServeDir::new(&web_dir).append_index_html_on_directories(true),
        )
        .with_state(state);

    let addr: SocketAddr = bind.parse()?;
    info!(addr = %addr, "serving dashboard");
    info!(url = %format!("http://{addr}/"), "open in browser");

    let listener = TcpListener::bind(&addr).await?;
    axum::serve(listener, app).await?;
    Ok(())
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------

async fn ws_handler(
    ws: WebSocketUpgrade,
    State(state): State<AppState>,
) -> Response {
    ws.on_upgrade(move |socket| ws_stream(socket, state))
}

async fn ws_stream(socket: WebSocket, state: AppState) {
    let (mut tx, _rx) = socket.split();
    let period = tokio::time::Duration::from_secs_f64(1.0 / 60.0);

    loop {
        let snap = state.backend.sample();
        let json = match serde_json::to_string(&snap) {
            Ok(j) => j,
            Err(_) => continue,
        };
        if tx.send(Message::Text(json.into())).await.is_err() {
            break;
        }
        tokio::time::sleep(period).await;
    }
}

// ---------------------------------------------------------------------------
// REST: devices
// ---------------------------------------------------------------------------

#[derive(serde::Serialize)]
struct DevicesResp {
    devices: Vec<DeviceInfo>,
}

#[derive(serde::Serialize)]
struct DeviceInfo {
    path: String,
    name: String,
}

async fn list_devices(
    State(_state): State<AppState>,
) -> Response {
    let mut out = Vec::new();
    for d in crate::input::list_ffb_devices() {
        out.push(DeviceInfo { path: d.path.clone(), name: d.name.clone() });
    }
    Json(DevicesResp { devices: out }).into_response()
}

// ---------------------------------------------------------------------------
// REST: gains
// ---------------------------------------------------------------------------

#[derive(serde::Serialize)]
struct GainsResp {
    gains: Vec<crate::gains::GainInfo>,
    observe: bool,
}

async fn gains_get(State(state): State<AppState>) -> Response {
    let path = state
        .backend
        .state_arc()
        .lock()
        .map(|s| s.real_path.clone())
        .unwrap_or_default();
    let observe = {
        let state_arc = state.backend.state_arc();
        let s = state_arc.lock().unwrap();
        matches!(s.mode, ProxyMode::Observe)
    };
    let gains = crate::gains::probe(&path);
    Json(GainsResp { gains, observe }).into_response()
}

#[derive(serde::Deserialize)]
struct GainPostBody {
    key:   String,
    value: u32,
}

async fn gains_post(
    State(state): State<AppState>,
    Json(body):  Json<GainPostBody>,
) -> Response {
    let path = {
        let state_arc = state.backend.state_arc();
        let s = state_arc.lock().unwrap();
        s.real_path.clone()
    };
    let ok = crate::gains::set_gain(&path, &body.key, body.value);
    let gains = crate::gains::probe(&path);
    let body = serde_json::json!({ "ok": ok, "gains": gains });
    (StatusCode::OK, Json(body)).into_response()
}
