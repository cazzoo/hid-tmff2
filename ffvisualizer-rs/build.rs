use std::io::{self, Write};

fn main() {
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=CARGO_CFG_TARGET_POINTER_WIDTH");

    assert_ff_effect_size();
}

fn assert_ff_effect_size() {
    let ptr_width = match option_env!("CARGO_CFG_TARGET_POINTER_WIDTH") {
        Some(w) => w.parse::<u32>().unwrap_or(64),
        None => return,
    };

    let expected = if ptr_width == 32 { 44 } else { 60 };
    // libc's input_event size must be writable into build script output.
    let _ = (ptr_width, expected);

    writeln!(
        io::stdout(),
        "cargo:warning=ff_effect expected {} bytes on {}-bit (assert at runtime)",
        expected, ptr_width
    )
    .ok();
}
