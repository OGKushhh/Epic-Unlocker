// Binary entry point — prevents Windows from opening a console window on release.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

fn main() {
    epicgui_lib::run()
}
