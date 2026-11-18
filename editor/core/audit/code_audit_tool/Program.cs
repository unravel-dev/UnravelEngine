using System.Text;
using System.Text.Json;

namespace Unravel.CodeAudit
{
    /// <summary>
    /// Reads a compiled script library and writes what its update methods reach, as JSON:
    /// <c>Unravel.CodeAudit --assembly app.dll [--references folder]... --out result.json</c>.
    /// Exit code 0 when the result was written, 2 for wrong arguments, 3 when the assembly could not be read.
    /// </summary>
    public static class Program
    {
        /// <summary>The output format; the editor refuses another.</summary>
        public const int FormatVersion = 1;

        public static int Main(string[] args)
        {
            string assembly = null;
            string output = null;
            var references = new List<string>();
            for (int i = 0; i < args.Length; i++)
            {
                var hasValue = i + 1 < args.Length;
                switch (args[i])
                {
                    case "--assembly" when hasValue:
                        assembly = args[++i];
                        break;
                    case "--references" when hasValue:
                        references.Add(args[++i]);
                        break;
                    case "--out" when hasValue:
                        output = args[++i];
                        break;
                    default:
                        Console.Error.WriteLine($"Unknown or incomplete argument: {args[i]}");
                        return 2;
                }
            }
            if (string.IsNullOrEmpty(assembly) || string.IsNullOrEmpty(output))
            {
                Console.Error.WriteLine("Usage: Unravel.CodeAudit --assembly <dll> [--references <folder>]... --out <json>");
                return 2;
            }

            AnalysisResult result;
            try
            {
                result = CodeAnalyzer.Analyze(assembly, references);
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"The assembly could not be read: {ex.Message}");
                return 3;
            }

            File.WriteAllText(output, WriteJson(result), new UTF8Encoding(false));
            return 0;
        }

        private static string WriteJson(AnalysisResult result)
        {
            using var stream = new MemoryStream();
            using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = false }))
            {
                writer.WriteStartObject();
                writer.WriteNumber("version", FormatVersion);
                writer.WriteNumber("methods", result.Methods);
                writer.WriteStartArray("entry_points");
                foreach (var point in result.EntryPoints)
                {
                    writer.WriteStartObject();
                    writer.WriteString("method", point.Method);
                    writer.WriteString("file", point.File ?? "");
                    writer.WriteNumber("line", point.Line);
                    writer.WriteNumber("reachable", point.Reachable);
                    writer.WriteEndObject();
                }
                writer.WriteEndArray();
                writer.WriteStartArray("findings");
                foreach (var finding in result.Findings)
                {
                    writer.WriteStartObject();
                    writer.WriteString("rule", finding.Rule);
                    writer.WriteString("method", finding.Method);
                    writer.WriteString("file", finding.File ?? "");
                    writer.WriteNumber("line", finding.Line);
                    writer.WriteString("target", finding.Target ?? "");
                    writer.WriteString("detail", finding.Detail ?? "");
                    writer.WriteStartArray("path");
                    foreach (var step in finding.Path)
                    {
                        writer.WriteStringValue(step);
                    }
                    writer.WriteEndArray();
                    writer.WriteEndObject();
                }
                writer.WriteEndArray();
                writer.WriteStartArray("errors");
                foreach (var error in result.Errors)
                {
                    writer.WriteStringValue(error);
                }
                writer.WriteEndArray();
                writer.WriteEndObject();
            }
            return Encoding.UTF8.GetString(stream.ToArray());
        }
    }
}
