//! FFB Visualizer — Linux FFB wheel visualizer (Rust reimplementation).

use std::sync::Arc;

use tracing::{error, info};

use ffb_visualizer::input::list_ffb_devices;
use ffb_visualizer::proxy::ProxyBackend;
use ffb_visualizer::server::{serve, AppState};

fn pick_device(cli: Option<&str>) -> Option<String> {
    let devs = list_ffb_devices();
    if devs.is_empty() {
        eprintln!("No FFB-capable input devices found.");
        return None;
    }
    if let Some(id) = cli {
        return devs.into_iter().find(|d| d.path == id).map(|d| d.path.clone());
    }
    let keywords: &[&str] = &["wheel", "thrustmaster", "logitech", "fanatec",
                              "t300", "t500", "g29", "g920", "t248"];
    for d in &devs {
        let name_lower = d.name.to_lowercase();
        if keywords.iter().any(|k| name_lower.contains(k)) {
            return Some(d.path.clone());
        }
    }
    Some(devs[0].path.clone())
}

fn main() -> anyhow::Result<()> {
    use tracing_subscriber::{layer::SubscriberExt, util::SubscriberInitExt, EnvFilter};
    tracing_subscriber::registry()
        .with(EnvFilter::try_from_default_env()
            .unwrap_or_else(|_| EnvFilter::new("ffb_visualizer=info")))
        .with(tracing_subscriber::fmt::layer())
        .try_init()?;

    let args: Vec<String> = std::env::args().collect();

    let mut device:  Option<String> = None;
    let mut observe: bool = false;
    let mut host:    &str = "127.0.0.1";
    let mut port:    u16 = 8000;
    let mut verbose: bool = false;

    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "-d" | "--device" => { i += 1; device = args.get(i).cloned(); }
            "-o" | "--observe" => { observe = true; }
            "-H" | "--host"    => { i += 1; host = args.get(i).map(|s| s.as_str()).unwrap_or(host); }
            "-p" | "--port"    => {
                i += 1;
                port = args.get(i).and_then(|s| s.parse().ok()).unwrap_or(port);
            }
            "-v" | "--verbose" => { verbose = true; }
            _ => {
                eprintln!("Usage: ffb-visualizer [-d PATH] [-o] [-H HOST] [-p PORT] [-v] [-h]");
                std::process::exit(1);
            }
        }
        i += 1;
    }

    // Pick device
    let real_path = match pick_device(device.as_deref()) {
        Some(p) => p,
        None => std::process::exit(1),
    };

    let mode_str = if observe { "observe" } else { "proxy" };
    println!("FFB Visualizer — {mode_str} mode");
    println!("  real device: {real_path}");
    if !observe {
        println!("  (virtual device node will appear once proxy is running)");
    }
    println!("\n  Dashboard:  http://{host}:{port}/\n");

    // Build and start the proxy backend
    let backend = Arc::new(ProxyBackend::new(real_path.clone(), observe));
    backend.start();

    // Serve the web dashboard (blocks on the axum HTTP server)
    serve(&format!("{host}:{port}"), backend).await?;

    Ok(())
}
