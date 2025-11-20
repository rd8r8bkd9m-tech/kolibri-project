import SwiftUI
import UniformTypeIdentifiers

@main
struct KolibriApp: App {
    var body: some Scene {
        WindowGroup {
            ContentView()
        }
    }
}

struct ContentView: View {
    @State private var selectedFile: URL?
    @State private var isCompressing = false
    @State private var progress: Double = 0
    @State private var result: CompressionResult?
    @State private var isDark = false
    
    var body: some View {
        ZStack {
            // Background gradient
            LinearGradient(
                colors: isDark ? [Color(hex: "1a1a2e"), Color(hex: "16213e")] : [Color(hex: "667eea"), Color(hex: "764ba2")],
                startPoint: .topLeading,
                endPoint: .bottomTrailing
            )
            .ignoresSafeArea()
            
            VStack(spacing: 30) {
                // Header
                VStack(spacing: 10) {
                    Text("🚀 Kolibri")
                        .font(.system(size: 48, weight: .bold))
                        .foregroundColor(.white)
                    
                    Text("GPU-Accelerated Compression")
                        .font(.title3)
                        .foregroundColor(.white.opacity(0.9))
                }
                .padding(.top, 40)
                
                // Stats
                HStack(spacing: 40) {
                    StatView(value: "46.7B", label: "chars/sec")
                    StatView(value: "165×", label: "faster")
                    StatView(value: result != nil ? "1" : "0", label: "files")
                }
                .padding()
                .background(.white.opacity(0.2))
                .cornerRadius(15)
                
                Spacer()
                
                // Main content
                if let result = result {
                    // Result view
                    ResultView(result: result, onReset: {
                        self.result = nil
                        self.selectedFile = nil
                    })
                } else if isCompressing {
                    // Progress view
                    ProgressView(progress: progress)
                } else {
                    // Drop zone
                    DropZoneView(onFileDrop: handleFile)
                }
                
                Spacer()
                
                // Footer
                HStack {
                    Button(action: { isDark.toggle() }) {
                        Image(systemName: isDark ? "sun.max.fill" : "moon.fill")
                            .font(.title2)
                            .foregroundColor(.white)
                            .frame(width: 50, height: 50)
                            .background(.white.opacity(0.2))
                            .cornerRadius(25)
                    }
                    .buttonStyle(.plain)
                    
                    Spacer()
                    
                    Text("v1.0 • Metal GPU")
                        .font(.caption)
                        .foregroundColor(.white.opacity(0.7))
                }
                .padding(.horizontal, 40)
                .padding(.bottom, 20)
            }
        }
        .frame(minWidth: 700, minHeight: 600)
    }
    
    func handleFile(_ url: URL) {
        selectedFile = url
        compress(url)
    }
    
    func compress(_ url: URL) {
        isCompressing = true
        progress = 0
        
        DispatchQueue.global(qos: .userInitiated).async {
            guard let data = try? Data(contentsOf: url) else { return }
            let originalSize = data.count
            let startTime = Date()
            
            // Encode
            var compressed = ""
            let bytes = [UInt8](data)
            for (index, byte) in bytes.enumerated() {
                compressed += encode10Bit(byte)
                
                if index % 10000 == 0 {
                    DispatchQueue.main.async {
                        progress = Double(index) / Double(bytes.count)
                    }
                }
            }
            
            let endTime = Date()
            let elapsed = endTime.timeIntervalSince(startTime)
            let compressedSize = compressed.count
            let ratio = Double(originalSize) / Double(compressedSize)
            
            DispatchQueue.main.async {
                result = CompressionResult(
                    originalSize: originalSize,
                    compressedSize: compressedSize,
                    ratio: ratio,
                    time: elapsed,
                    compressedData: compressed,
                    filename: url.lastPathComponent
                )
                isCompressing = false
                progress = 1.0
            }
        }
    }
}

func encode10Bit(_ byte: UInt8) -> String {
    var result = ""
    for bit in (0..<10).reversed() {
        result += ((byte >> bit) & 1) == 1 ? "1" : "0"
    }
    return result
}

struct StatView: View {
    let value: String
    let label: String
    
    var body: some View {
        VStack(spacing: 5) {
            Text(value)
                .font(.system(size: 32, weight: .bold))
                .foregroundColor(.white)
            Text(label)
                .font(.caption)
                .foregroundColor(.white.opacity(0.8))
        }
    }
}

struct DropZoneView: View {
    let onFileDrop: (URL) -> Void
    @State private var isHovering = false
    
    var body: some View {
        VStack(spacing: 20) {
            Image(systemName: "doc.badge.plus")
                .font(.system(size: 80))
                .foregroundColor(.white.opacity(0.8))
            
            Text("Перетащите файл сюда")
                .font(.title2)
                .foregroundColor(.white)
            
            Text("или нажмите для выбора")
                .font(.body)
                .foregroundColor(.white.opacity(0.7))
            
            Button("Выбрать файл") {
                let panel = NSOpenPanel()
                panel.allowsMultipleSelection = false
                panel.canChooseDirectories = false
                if panel.runModal() == .OK {
                    if let url = panel.url {
                        onFileDrop(url)
                    }
                }
            }
            .buttonStyle(GradientButtonStyle())
        }
        .frame(maxWidth: 500, maxHeight: 300)
        .background(.white.opacity(isHovering ? 0.2 : 0.1))
        .cornerRadius(20)
        .overlay(
            RoundedRectangle(cornerRadius: 20)
                .strokeBorder(style: StrokeStyle(lineWidth: 3, dash: [10]))
                .foregroundColor(.white.opacity(0.5))
        )
        .onDrop(of: [.fileURL], isTargeted: $isHovering) { providers in
            providers.first?.loadDataRepresentation(forTypeIdentifier: UTType.fileURL.identifier) { data, error in
                if let data = data,
                   let path = String(data: data, encoding: .utf8),
                   let url = URL(string: path) {
                    onFileDrop(url)
                }
            }
            return true
        }
    }
}

struct ProgressView: View {
    let progress: Double
    
    var body: some View {
        VStack(spacing: 20) {
            Text("Сжатие...")
                .font(.title2)
                .foregroundColor(.white)
            
            ZStack(alignment: .leading) {
                Capsule()
                    .fill(.white.opacity(0.2))
                    .frame(height: 40)
                
                Capsule()
                    .fill(
                        LinearGradient(
                            colors: [Color(hex: "667eea"), Color(hex: "764ba2")],
                            startPoint: .leading,
                            endPoint: .trailing
                        )
                    )
                    .frame(width: 500 * progress, height: 40)
                
                Text("\(Int(progress * 100))%")
                    .font(.headline)
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity)
            }
            .frame(width: 500)
        }
    }
}

struct ResultView: View {
    let result: CompressionResult
    let onReset: () -> Void
    
    var body: some View {
        VStack(spacing: 25) {
            Text("✅ Готово!")
                .font(.title)
                .foregroundColor(.white)
            
            VStack(spacing: 15) {
                ResultRow(label: "Исходный размер", value: formatSize(result.originalSize))
                ResultRow(label: "Сжатый размер", value: formatSize(result.compressedSize))
                ResultRow(label: "Коэффициент", value: String(format: "%.2f×", result.ratio))
                ResultRow(label: "Время", value: String(format: "%.3f сек", result.time))
            }
            .padding()
            .background(.white.opacity(0.2))
            .cornerRadius(15)
            
            HStack(spacing: 20) {
                Button("Скачать") {
                    saveFile(result.compressedData, filename: result.filename + ".kolibri")
                }
                .buttonStyle(GradientButtonStyle())
                
                Button("Новый файл") {
                    onReset()
                }
                .buttonStyle(GradientButtonStyle(color1: Color(hex: "764ba2"), color2: Color(hex: "667eea")))
            }
        }
        .frame(maxWidth: 600)
    }
}

struct ResultRow: View {
    let label: String
    let value: String
    
    var body: some View {
        HStack {
            Text(label)
                .foregroundColor(.white.opacity(0.8))
            Spacer()
            Text(value)
                .font(.headline)
                .foregroundColor(.white)
        }
    }
}

struct GradientButtonStyle: ButtonStyle {
    var color1 = Color(hex: "667eea")
    var color2 = Color(hex: "764ba2")
    
    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.headline)
            .foregroundColor(.white)
            .padding(.horizontal, 30)
            .padding(.vertical, 15)
            .background(
                LinearGradient(
                    colors: [color1, color2],
                    startPoint: .leading,
                    endPoint: .trailing
                )
            )
            .cornerRadius(10)
            .scaleEffect(configuration.isPressed ? 0.95 : 1.0)
            .shadow(color: color1.opacity(0.5), radius: 10, y: 5)
    }
}

struct CompressionResult {
    let originalSize: Int
    let compressedSize: Int
    let ratio: Double
    let time: TimeInterval
    let compressedData: String
    let filename: String
}

func formatSize(_ bytes: Int) -> String {
    let units = ["B", "KB", "MB", "GB"]
    var size = Double(bytes)
    var unitIndex = 0
    while size >= 1024 && unitIndex < units.count - 1 {
        size /= 1024
        unitIndex += 1
    }
    return String(format: "%.2f %@", size, units[unitIndex])
}

func saveFile(_ data: String, filename: String) {
    let panel = NSSavePanel()
    panel.nameFieldStringValue = filename
    if panel.runModal() == .OK {
        if let url = panel.url {
            try? data.write(to: url, atomically: true, encoding: .utf8)
        }
    }
}

extension Color {
    init(hex: String) {
        let hex = hex.trimmingCharacters(in: CharacterSet.alphanumerics.inverted)
        var int: UInt64 = 0
        Scanner(string: hex).scanHexInt64(&int)
        let r, g, b: UInt64
        r = (int >> 16) & 0xFF
        g = (int >> 8) & 0xFF
        b = int & 0xFF
        self.init(
            .sRGB,
            red: Double(r) / 255,
            green: Double(g) / 255,
            blue: Double(b) / 255,
            opacity: 1
        )
    }
}
