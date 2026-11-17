// Maschine Jam MIDI.app: the driver (Sources/Driver) as a menu bar app that starts at login.

import AppKit
import ServiceManagement

final class AppDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    let menu = NSMenu()
    let statusLine = NSMenuItem(title: "", action: nil, keyEquivalent: "")
    let loginItem = NSMenuItem(title: "Start at Login", action: #selector(toggleLogin), keyEquivalent: "")
    let testItem = NSMenuItem(title: "Colour Test", action: #selector(toggleTest), keyEquivalent: "")

    func applicationDidFinishLaunching(_ note: Notification) {
        // Another copy (or the command line jam-midi) would make a second port with the same name.
        let me = Bundle.main.bundleIdentifier ?? ""
        if NSRunningApplication.runningApplications(withBundleIdentifier: me).count > 1 {
            NSApp.terminate(nil); return
        }

        menu.delegate = self
        statusLine.isEnabled = false
        menu.addItem(statusLine)
        let port = NSMenuItem(title: "MIDI port: Maschine Jam MIDI", action: nil, keyEquivalent: "")
        port.isEnabled = false
        menu.addItem(port)
        menu.addItem(.separator())
        testItem.target = self
        menu.addItem(testItem)
        loginItem.target = self
        menu.addItem(loginItem)
        let map = NSMenuItem(title: "MIDI Map…", action: #selector(openMap), keyEquivalent: "")
        map.target = self
        menu.addItem(map)
        menu.addItem(.separator())
        let quit = NSMenuItem(title: "Quit Maschine Jam MIDI", action: #selector(quit), keyEquivalent: "q")
        quit.target = self
        menu.addItem(quit)
        item.menu = menu

        onConnectionChange = { [weak self] _ in self?.update() }
        startDriver()
        update()

        // The first time it runs, it adds itself to the login items, so the Jam just works after a restart.
        let firstRunKey = "loginItemOffered"
        if !UserDefaults.standard.bool(forKey: firstRunKey) {
            UserDefaults.standard.set(true, forKey: firstRunKey)
            try? SMAppService.mainApp.register()
        }
    }

    func update() {
        statusLine.title = jamConnected ? "Maschine Jam connected" : "Maschine Jam not connected"
        let symbol = jamConnected ? "square.grid.3x3.fill" : "square.grid.3x3"
        let image = NSImage(systemSymbolName: symbol, accessibilityDescription: "Maschine Jam MIDI")
        image?.isTemplate = true
        item.button?.image = image
        item.button?.toolTip = "Maschine Jam MIDI: " + (jamConnected ? "connected" : "not connected")
        loginItem.state = SMAppService.mainApp.status == .enabled ? .on : .off
        testItem.state = colourTest ? .on : .off
    }

    func menuWillOpen(_ menu: NSMenu) { update() }

    @objc func toggleLogin() {
        do {
            if SMAppService.mainApp.status == .enabled { try SMAppService.mainApp.unregister() }
            else { try SMAppService.mainApp.register() }
        } catch {
            let a = NSAlert()
            a.messageText = "Could not change the login item"
            a.informativeText = error.localizedDescription + "\n\nYou can also set it in System Settings → General → Login Items."
            a.runModal()
        }
        update()
    }

    @objc func toggleTest() { colourTest.toggle(); update() }

    @objc func openMap() {
        NSWorkspace.shared.open(URL(string: "https://github.com/tijnisfijn/maschine-jam-midi/blob/main/MIDI-MAP.md")!)
    }

    @objc func quit() { NSApp.terminate(nil) }

    func applicationWillTerminate(_ note: Notification) { ledsOff() }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.setActivationPolicy(.accessory)
app.run()
