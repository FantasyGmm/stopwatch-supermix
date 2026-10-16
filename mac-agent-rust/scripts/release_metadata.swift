import Foundation
import Darwin

enum MetadataError: Error, CustomStringConvertible {
    case usage
    case invalidJSON
    case missingField(String)
    case invalidBuild(String)

    var description: String {
        switch self {
        case .usage: return "usage: release_metadata.swift validate FILE | get FILE FIELD | write FILE VERSION FINGERPRINT FILENAME SHA COMMIT BUILD SIGNING BUILT_AT RUNTIME_SELF_TEST"
        case .invalidJSON: return "invalid release metadata JSON"
        case .missingField(let name): return "missing release metadata field: \(name)"
        case .invalidBuild(let value): return "invalid release build number: \(value)"
        }
    }
}

func readObject(_ path: String) throws -> [String: Any] {
    let data = try Data(contentsOf: URL(fileURLWithPath: path))
    guard let object = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
        throw MetadataError.invalidJSON
    }
    return object
}

func writeObject(_ object: [String: Any], to path: String) throws {
    let data = try JSONSerialization.data(withJSONObject: object, options: [.prettyPrinted, .sortedKeys])
    try (String(decoding: data, as: UTF8.self) + "\n").write(toFile: path, atomically: true, encoding: .utf8)
}

do {
    let arguments = Array(CommandLine.arguments.dropFirst())
    guard let command = arguments.first else { throw MetadataError.usage }
    switch command {
    case "validate":
        guard arguments.count == 2 else { throw MetadataError.usage }
        _ = try readObject(arguments[1])
    case "get":
        guard arguments.count == 3 else { throw MetadataError.usage }
        let object = try readObject(arguments[1])
        guard let value = object[arguments[2]] else { throw MetadataError.missingField(arguments[2]) }
        if let string = value as? String {
            print(string)
        } else if let number = value as? NSNumber {
            print(number.stringValue)
        } else {
            throw MetadataError.missingField(arguments[2])
        }
    case "write":
        guard arguments.count == 11 else { throw MetadataError.usage }
        guard let build = Int(arguments[7]), build > 0 else { throw MetadataError.invalidBuild(arguments[7]) }
        guard arguments[10] == "passed" || arguments[10] == "skipped" else { throw MetadataError.usage }
        let object: [String: Any] = [
            "version": arguments[2],
            "source_fingerprint": arguments[3],
            "filename": arguments[4],
            "sha256": arguments[5],
            "source_commit": arguments[6],
            "build": build,
            "signing": arguments[8],
            "built_at": arguments[9],
            "runtime_self_test": arguments[10],
        ]
        try writeObject(object, to: arguments[1])
    default:
        throw MetadataError.usage
    }
} catch {
    fputs("\(error)\n", stderr)
    exit(2)
}
