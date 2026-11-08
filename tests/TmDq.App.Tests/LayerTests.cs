using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using TmDq.App.ViewModels;
using Xunit;

namespace TmDq.App.Tests
{
    /// <summary>
    /// The application's layers (docs/claude.md, File Organization): the models know no view model, view or service; the
    /// view models and the service interfaces know no view. None of them knows Avalonia but its <c>Color</c>, a plain
    /// value the series, the prompts and the template file carry. Only the views and <c>Platform</c> reach Avalonia. Each type
    /// is read as compiled: its signatures (base, interfaces, fields, properties, events, parameters and results) and
    /// every type and member its methods' code names, the compiler's nested types (lambdas, async state machines)
    /// counted as the type's own, so a brush made inside a method is caught as surely as one returned.
    /// </summary>
    public sealed class LayerTests
    {
        private const string App = "TmDq.App";

        /// <summary>Avalonia's types the view models, services and models may use: the colour and its named values.</summary>
        private static readonly HashSet<string> AllowedAvalonia = new(StringComparer.Ordinal) { "Avalonia.Media.Color", "Avalonia.Media.Colors" };

        private static readonly Assembly AppAssembly = typeof(MainWindowViewModel).Assembly;

        private static bool InLayer(Type t, string layer) =>
            t.Namespace is { } ns && (ns == App + "." + layer || ns.StartsWith(App + "." + layer + ".", StringComparison.Ordinal));

        private static bool ForbiddenAvalonia(Type t) =>
            t.Assembly.GetName().Name!.StartsWith("Avalonia", StringComparison.Ordinal) && !AllowedAvalonia.Contains(t.FullName ?? "");

        [Fact]
        public void ModelsReachNoViewModelViewOrServiceAndNoAvaloniaButColor()
        {
            string[] wrong = Breaches(t => InLayer(t, "Models"),
                                      r => InLayer(r, "ViewModels") || InLayer(r, "Views") || InLayer(r, "Services") || InLayer(r, "Platform") || ForbiddenAvalonia(r));
            Assert.True(wrong.Length == 0, "a model reaches outside its layer:\n" + string.Join("\n", wrong));
        }

        [Fact]
        public void ViewModelsAndServicesReachNoViewAndNoAvaloniaButColor()
        {
            string[] wrong = Breaches(t => InLayer(t, "ViewModels") || InLayer(t, "Services"),
                                      r => InLayer(r, "Views") || InLayer(r, "Platform") || ForbiddenAvalonia(r));
            Assert.True(wrong.Length == 0, "a view model or service reaches a view or Avalonia:\n" + string.Join("\n", wrong));
        }

        /// <summary>The reading can see: a view's base type, a type a method's code alone names (a platform picker's
        /// <c>StorageProvider</c> is reached only inside its methods), and the layers hold the types they should.</summary>
        [Fact]
        public void TheReadingSeesSignaturesAndCode()
        {
            Assert.Contains(References(typeof(Views.MainWindow)), r => r.FullName == "Avalonia.Controls.Window");
            Assert.Contains(References(typeof(Platform.StorageRecordingPicker)), r => r.FullName == "Avalonia.Platform.Storage.IStorageProvider");
            Assert.Contains(References(typeof(ViewModels.Plot.PlotViewModel)), r => r.FullName == "Avalonia.Media.Color");
            Assert.Contains(References(typeof(Models.Templates.TemplateBatch)), r => r == typeof(Models.Templates.ProcessingTemplate));
            Assert.Contains(AppAssembly.GetTypes(), t => InLayer(t, "Models"));
            Assert.Contains(AppAssembly.GetTypes(), t => InLayer(t, "ViewModels"));
            Assert.Contains(AppAssembly.GetTypes(), t => InLayer(t, "Services"));
        }

        /// <summary>"Type -> what it reaches" for every type of the layer that reaches what it must not.</summary>
        private static string[] Breaches(Func<Type, bool> layer, Func<Type, bool> forbidden) =>
            AppAssembly.GetTypes().Where(t => t.DeclaringType is null && layer(t))
                .SelectMany(t => References(t).Where(forbidden).Select(r => t.FullName + " -> " + r.FullName))
                .Distinct().Order(StringComparer.Ordinal).ToArray();

        private const BindingFlags Declared = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;

        /// <summary>Every type <paramref name="t"/> names, in its signatures and in its methods' code, its nested types'
        /// (the compiler's lambdas and state machines, where an async method's code is) included.</summary>
        private static HashSet<Type> References(Type t)
        {
            var found = new HashSet<Type>();
            foreach (Type nested in t.GetNestedTypes(Declared)) found.UnionWith(References(nested));
            void Add(Type? x)
            {
                if (x is null || x.IsGenericParameter) return;
                if (x.HasElementType) { Add(x.GetElementType()); return; }
                if (!found.Add(x)) return;
                if (x.IsGenericType && !x.IsGenericTypeDefinition)
                {
                    Add(x.GetGenericTypeDefinition());
                    foreach (Type a in x.GetGenericArguments()) Add(a);
                }
            }

            Add(t.BaseType);
            foreach (Type i in t.GetInterfaces()) Add(i);
            foreach (CustomAttributeData a in t.GetCustomAttributesData()) Add(a.AttributeType);
            foreach (FieldInfo f in t.GetFields(Declared)) Add(f.FieldType);
            foreach (PropertyInfo p in t.GetProperties(Declared)) Add(p.PropertyType);
            foreach (EventInfo e in t.GetEvents(Declared)) Add(e.EventHandlerType);
            foreach (MethodBase m in t.GetMethods(Declared).Cast<MethodBase>().Concat(t.GetConstructors(Declared)))
            {
                if (m is MethodInfo mi) Add(mi.ReturnType);
                foreach (ParameterInfo p in m.GetParameters()) Add(p.ParameterType);
                foreach (MemberInfo named in CodeReferences(t, m))
                {
                    switch (named)
                    {
                        case Type type: Add(type); break;
                        case FieldInfo f: Add(f.DeclaringType); Add(f.FieldType); break;
                        case MethodBase callee:
                            Add(callee.DeclaringType);
                            if (callee is MethodInfo cm) Add(cm.ReturnType);
                            foreach (ParameterInfo p in callee.GetParameters()) Add(p.ParameterType);
                            if (callee.IsGenericMethod) foreach (Type a in callee.GetGenericArguments()) Add(a);
                            break;
                    }
                }
            }
            return found;
        }

        private static readonly Dictionary<short, OpCode> OpCodesByValue =
            typeof(OpCodes).GetFields(BindingFlags.Public | BindingFlags.Static).Select(f => (OpCode)f.GetValue(null)!).ToDictionary(o => o.Value);

        /// <summary>The types, fields and methods a method's IL names (the tokens of ldtoken, newobj, call, ldfld, box, ...).</summary>
        private static IEnumerable<MemberInfo> CodeReferences(Type owner, MethodBase m)
        {
            byte[]? il = m.GetMethodBody()?.GetILAsByteArray();
            if (il is null) yield break;
            Type[]? typeArgs = owner.IsGenericType ? owner.GetGenericArguments() : null;
            Type[]? methodArgs = m.IsGenericMethod ? m.GetGenericArguments() : null;
            int i = 0;
            while (i < il.Length)
            {
                short value = il[i] == 0xFE ? (short)(0xFE00 | il[i + 1]) : il[i];
                OpCode op = OpCodesByValue[unchecked(value)];
                i += op.Size;
                switch (op.OperandType)
                {
                    case OperandType.InlineType:
                    case OperandType.InlineMethod:
                    case OperandType.InlineField:
                    case OperandType.InlineTok:
                        MemberInfo? member = m.Module.ResolveMember(BitConverter.ToInt32(il, i), typeArgs, methodArgs);
                        if (member is not null) yield return member;
                        i += 4;
                        break;
                    case OperandType.InlineSwitch:
                        i += 4 + 4 * BitConverter.ToInt32(il, i);
                        break;
                    case OperandType.InlineI8:
                    case OperandType.InlineR:
                        i += 8;
                        break;
                    case OperandType.InlineNone:
                        break;
                    case OperandType.ShortInlineBrTarget:
                    case OperandType.ShortInlineI:
                    case OperandType.ShortInlineVar:
                        i += 1;
                        break;
                    case OperandType.InlineVar:
                        i += 2;
                        break;
                    default:   // InlineBrTarget, InlineI, InlineSig, InlineString, ShortInlineR
                        i += 4;
                        break;
                }
            }
        }
    }
}
