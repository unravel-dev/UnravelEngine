using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection.Metadata;
using System.Reflection.Metadata.Ecma335;
using System.Reflection.PortableExecutable;
using System.Text;

namespace Unravel.Core
{
    /// <summary>
    /// Reads which source file declares each type of a script library from the portable pdb the
    /// compiler writes beside it. The editor's Script row and tools open a script's source through
    /// it, so a script class needs no attribute for that.
    /// </summary>
    /// <remarks>
    /// Reads the library files, not the loaded assembly, so it holds no game types.
    /// </remarks>
    internal static class ScriptSources
    {
        /// <summary>
        /// Kind of the pdb record that lists the files of a type no method body places: a type
        /// with only fields, or the parts of a partial type without methods.
        /// </summary>
        private static readonly Guid TypeDefinitionDocumentsKind = new Guid("932E74BC-DBA9-4478-8D46-0F32A7BAB3D3");

        private const string PdbExtension = ".pdb";

        /// <summary>
        /// Lists every type of a library with the source file that declares it, one
        /// "full name TAB path" per line. Nested types are named with '.', as the native side names
        /// them. Empty when the library has no portable pdb beside it.
        /// </summary>
        /// <param name="libraryPath">The library dll; its pdb has the same name.</param>
        internal static string internal_n2m_read(string libraryPath)
        {
            string pdbPath = Path.ChangeExtension(libraryPath, PdbExtension);
            if (!File.Exists(libraryPath) || !File.Exists(pdbPath))
            {
                return string.Empty;
            }
            try
            {
                return ReadSourceFiles(File.ReadAllBytes(libraryPath), File.ReadAllBytes(pdbPath));
            }
            catch (Exception ex)
            {
                Log.Warning($"The source files of the script types in {libraryPath} cannot be read from its symbols: {ex.Message}");
                return string.Empty;
            }
        }

        private static string ReadSourceFiles(byte[] libraryBytes, byte[] pdbBytes)
        {
            using var library = new PEReader(new MemoryStream(libraryBytes));
            using var symbols = MetadataReaderProvider.FromPortablePdbStream(new MemoryStream(pdbBytes));
            MetadataReader metadata = library.GetMetadataReader();
            MetadataReader pdb = symbols.GetMetadataReader();
            var text = new StringBuilder();
            foreach (TypeDefinitionHandle handle in metadata.TypeDefinitions)
            {
                TypeDefinition type = metadata.GetTypeDefinition(handle);
                string name = metadata.GetString(type.Name);
                // <Module>, closures and state machines are the compiler's, not a script's.
                if (name.IndexOf('<') >= 0)
                {
                    continue;
                }
                string file = FindSourceFile(pdb, handle, type, name);
                if (file != null)
                {
                    text.Append(GetFullName(metadata, type)).Append('\t').Append(file).Append('\n');
                }
            }
            return text.ToString();
        }

        /// <summary>
        /// The file a type is declared in: of the files its methods and declarations are in, the
        /// one named after the type (a partial type spans several), else the first.
        /// </summary>
        private static string FindSourceFile(MetadataReader pdb, TypeDefinitionHandle handle, TypeDefinition type, string name)
        {
            var files = new List<string>();
            foreach (MethodDefinitionHandle method in type.GetMethods())
            {
                AddMethodFiles(pdb, method, files);
            }
            AddDeclarationFiles(pdb, handle, files);
            if (files.Count == 0)
            {
                return null;
            }
            string named = files.Find(file => string.Equals(Path.GetFileNameWithoutExtension(file), name, StringComparison.OrdinalIgnoreCase));
            return named ?? files[0];
        }

        private static void AddMethodFiles(MetadataReader pdb, MethodDefinitionHandle method, List<string> files)
        {
            MethodDebugInformation info = pdb.GetMethodDebugInformation(method);
            if (!info.Document.IsNil)
            {
                AddFile(pdb, info.Document, files);
                return;
            }
            if (info.SequencePointsBlob.IsNil)
            {
                return;
            }
            // A method whose code comes from several files names no single document.
            foreach (SequencePoint point in info.GetSequencePoints())
            {
                AddFile(pdb, point.Document, files);
            }
        }

        private static void AddDeclarationFiles(MetadataReader pdb, TypeDefinitionHandle handle, List<string> files)
        {
            foreach (CustomDebugInformationHandle infoHandle in pdb.GetCustomDebugInformation(handle))
            {
                CustomDebugInformation info = pdb.GetCustomDebugInformation(infoHandle);
                if (pdb.GetGuid(info.Kind) != TypeDefinitionDocumentsKind)
                {
                    continue;
                }
                BlobReader blob = pdb.GetBlobReader(info.Value);
                while (blob.RemainingBytes > 0)
                {
                    AddFile(pdb, MetadataTokens.DocumentHandle(blob.ReadCompressedInteger()), files);
                }
            }
        }

        private static void AddFile(MetadataReader pdb, DocumentHandle document, List<string> files)
        {
            string file = pdb.GetString(pdb.GetDocument(document).Name);
            if (!files.Contains(file))
            {
                files.Add(file);
            }
        }

        private static string GetFullName(MetadataReader metadata, TypeDefinition type)
        {
            string name = metadata.GetString(type.Name);
            TypeDefinitionHandle declaring = type.GetDeclaringType();
            if (!declaring.IsNil)
            {
                return GetFullName(metadata, metadata.GetTypeDefinition(declaring)) + "." + name;
            }
            string space = metadata.GetString(type.Namespace);
            return space.Length == 0 ? name : space + "." + name;
        }
    }
}
