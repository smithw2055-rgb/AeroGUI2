module Aero.UI;

import Aero.Native;
import RealScript.Math;

internal class CompiledMathExpr {
    public int[] Ops;
    public int[] OpArgs;
    public float[] Consts;
    private float[] _stack;

    public CompiledMathExpr(int[] ops, int[] opArgs, float[] consts, int maxStack) {
        Ops = ops;
        OpArgs = opArgs;
        Consts = consts;
        int cap = maxStack + 4;
        if (cap < 16) { cap = 16; }
        _stack = new float[cap];
    }

    internal static float FloorF(float x) {
        int k = (int)x;
        float kf = (float)k;
        return (x < kf) ? (kf - 1.0f) : kf;
    }

    internal static float CeilF(float x) {
        int k = (int)x;
        float kf = (float)k;
        return (x > kf) ? (kf + 1.0f) : kf;
    }

    internal static float ExpF(float x) {
        float ln2 = 0.69314718f;
        int m = (int)FloorF(x / ln2 + 0.5f);
        float r = x - (float)m * ln2;
        float r2 = r * r;
        float r3 = r2 * r;
        float r4 = r2 * r2;
        float r5 = r4 * r;
        float r6 = r3 * r3;
        float poly = 1.0f + r + r2 * 0.5f + r3 * 0.16666667f + r4 * 0.04166667f + r5 * 0.00833333f + r6 * 0.00138889f;
        if (m >= 0) {
            int i = 0;
            while (i < m && i < 64) { poly = poly * 2.0f; i += 1; }
        } else {
            int i = 0;
            int negM = -m;
            while (i < negM && i < 64) { poly = poly * 0.5f; i += 1; }
        }
        return poly;
    }

    internal static float LnF(float x) {
        if (x <= 0.0f) { return 0.0f; }
        float ln2 = 0.69314718f;
        int k = 0;
        float v = x;
        while (v > 1.5f && k < 64) { v = v * 0.5f; k += 1; }
        while (v < 0.5f && k > -64) { v = v * 2.0f; k -= 1; }
        float u = (v - 1.0f) / (v + 1.0f);
        float u2 = u * u;
        float u3 = u * u2;
        float u5 = u3 * u2;
        float u7 = u5 * u2;
        float u9 = u7 * u2;
        float s = 2.0f * (u + u3 * 0.33333334f + u5 * 0.2f + u7 * 0.14285715f + u9 * 0.11111111f);
        return s + (float)k * ln2;
    }

    internal static float PowF(float b, float e) {
        if (e == 0.0f) { return 1.0f; }
        if (b == 0.0f) { return e > 0.0f ? 0.0f : 1.0f; }
        int ei = (int)e;
        if ((float)ei == e && ei >= -32 && ei <= 32) {
            float res = 1.0f;
            int cnt = ei < 0 ? -ei : ei;
            for (int i = 0; i < cnt; i += 1) { res = res * b; }
            return ei < 0 ? (1.0f / res) : res;
        }
        if (b < 0.0f) { return 0.0f; }
        return ExpF(e * LnF(b));
    }

    public float EvalSingle(float x) {
        return EvalCore(x, null, false);
    }

    public float EvalMulti(DependencyObject[] values) {
        return EvalCore(0.0f, values, true);
    }

    private float EvalCore(float x, DependencyObject[] values, bool isMulti) {
        if (Ops == null || Ops.Length == 0) { return 0.0f; }
        int sp = 0;
        int n = Ops.Length;
        for (int i = 0; i < n; i += 1) {
            int op = Ops[i];
            if (op == 1) {
                _stack[sp] = Consts[OpArgs[i]];
                sp += 1;
            } else if (op == 2) {
                int idx = OpArgs[i];
                if (!isMulti) {
                    _stack[sp] = idx == 0 ? x : 0.0f;
                } else if (values != null && idx >= 0 && idx < values.Length && values[idx] != null) {
                    _stack[sp] = DependencyObject.UnboxFloat(values[idx], 0.0f);
                } else {
                    _stack[sp] = 0.0f;
                }
                sp += 1;
            } else if (op == 3) {
                sp -= 1;
                _stack[sp - 1] = _stack[sp - 1] + _stack[sp];
            } else if (op == 4) {
                sp -= 1;
                _stack[sp - 1] = _stack[sp - 1] - _stack[sp];
            } else if (op == 5) {
                sp -= 1;
                _stack[sp - 1] = _stack[sp - 1] * _stack[sp];
            } else if (op == 6) {
                sp -= 1;
                float r = _stack[sp];
                _stack[sp - 1] = (r != 0.0f) ? (_stack[sp - 1] / r) : 0.0f;
            } else if (op == 7) {
                sp -= 1;
                float r = _stack[sp];
                if (r != 0.0f) {
                    float l = _stack[sp - 1];
                    _stack[sp - 1] = l - FloorF(l / r) * r;
                } else {
                    _stack[sp - 1] = 0.0f;
                }
            } else if (op == 8) {
                sp -= 1;
                _stack[sp - 1] = PowF(_stack[sp - 1], _stack[sp]);
            } else if (op == 9) {
                _stack[sp - 1] = -_stack[sp - 1];
            } else if (op == 10) {
                _stack[sp - 1] = FloorF(_stack[sp - 1]);
            } else if (op == 11) {
                _stack[sp - 1] = CeilF(_stack[sp - 1]);
            } else if (op == 12) {
                float a = _stack[sp - 1];
                _stack[sp - 1] = a >= 0.0f ? FloorF(a + 0.5f) : -FloorF(-a + 0.5f);
            } else if (op == 13) {
                _stack[sp - 1] = Math.Absf(_stack[sp - 1]);
            } else if (op == 14) {
                float a = _stack[sp - 1];
                _stack[sp - 1] = a >= 0.0f ? Math.Sqrtf(a) : 0.0f;
            } else if (op == 15) {
                _stack[sp - 1] = Math.Sinf(_stack[sp - 1]);
            } else if (op == 16) {
                _stack[sp - 1] = Math.Cosf(_stack[sp - 1]);
            } else if (op == 17) {
                sp -= 1;
                _stack[sp - 1] = Math.Minf(_stack[sp - 1], _stack[sp]);
            } else if (op == 18) {
                sp -= 1;
                _stack[sp - 1] = Math.Maxf(_stack[sp - 1], _stack[sp]);
            } else if (op == 19) {
                sp -= 2;
                _stack[sp - 1] = Math.Clampf(_stack[sp - 1], _stack[sp], _stack[sp + 1]);
            }
        }
        return sp > 0 ? _stack[0] : 0.0f;
    }
}

internal class MathExprParser {
    private static int _debugCompileCount = 0;

    public static int DebugCompileCount() { return _debugCompileCount; }
    public static void DebugResetCompileCount() { _debugCompileCount = 0; }

    private string _s = "";
    private int _p = 0;
    private int[] _ops;
    private int[] _opArgs;
    private float[] _consts;
    private int _depth = 0;
    private int _maxDepth = 1;

    private MathExprParser(string expr) {
        _s = expr == null ? "" : expr;
        _p = 0;
        _ops = new int[0];
        _opArgs = new int[0];
        _consts = new float[0];
    }

    public static CompiledMathExpr Compile(string expr) {
        _debugCompileCount += 1;
        var p = new MathExprParser(expr);
        p.CompileExpr();
        return new CompiledMathExpr(p._ops, p._opArgs, p._consts, p._maxDepth);
    }

    private void Emit(int op, int arg) {
        _ops.Push(op);
        _opArgs.Push(arg);
        if (op == 1 || op == 2) {
            _depth += 1;
            if (_depth > _maxDepth) { _maxDepth = _depth; }
        } else if (op == 19) {
            _depth -= 2;
        } else if (op != 9 && op != 10 && op != 11 && op != 12 && op != 13 && op != 14 && op != 15 && op != 16) {
            _depth -= 1;
        }
    }

    private void EmitConst(float v) {
        int cidx = _consts.Length;
        _consts.Push(v);
        Emit(1, cidx);
    }

    private void SkipWs() {
        while (_p < _s.Length) {
            string ch = _s.Substring(_p, 1);
            if (ch == " " || ch == "\t" || ch == "\r" || ch == "\n") { _p += 1; }
            else { break; }
        }
    }

    private string Peek() {
        SkipWs();
        if (_p >= _s.Length) { return ""; }
        return _s.Substring(_p, 1);
    }

    private void CompileExpr() {
        CompileTerm();
        while (true) {
            string op = Peek();
            if (op == "+") { _p += 1; CompileTerm(); Emit(3, 0); }
            else if (op == "-") { _p += 1; CompileTerm(); Emit(4, 0); }
            else { break; }
        }
    }

    private void CompileTerm() {
        CompilePower();
        while (true) {
            string op = Peek();
            if (op == "*") {
                _p += 1;
                CompilePower();
                Emit(5, 0);
            } else if (op == "/") {
                _p += 1;
                CompilePower();
                Emit(6, 0);
            } else if (op == "%") {
                _p += 1;
                CompilePower();
                Emit(7, 0);
            } else {
                break;
            }
        }
    }

    private void CompilePower() {
        CompileUnary();
        if (Peek() == "^") {
            _p += 1;
            CompilePower();
            Emit(8, 0);
        }
    }

    private void CompileUnary() {
        string op = Peek();
        if (op == "-") { _p += 1; CompileUnary(); Emit(9, 0); return; }
        if (op == "+") { _p += 1; CompileUnary(); return; }
        CompilePrimary();
    }

    private void CompilePrimary() {
        string ch = Peek();
        if (ch == "(") {
            _p += 1;
            CompileExpr();
            if (Peek() == ")") { _p += 1; }
            return;
        }
        if (ch == "{") {
            _p += 1;
            int idx = 0;
            while (_p < _s.Length) {
                string dch = _s.Substring(_p, 1);
                if (dch == "}") { _p += 1; break; }
                int dv = ParseUtil.DigitValue(dch);
                if (dv >= 0) { idx = idx * 10 + dv; }
                _p += 1;
            }
            Emit(2, idx);
            return;
        }
        if (ParseUtil.DigitValue(ch) >= 0 || ch == ".") {
            int start = _p;
            bool seenDot = false;
            while (_p < _s.Length) {
                string nc = _s.Substring(_p, 1);
                if (ParseUtil.DigitValue(nc) >= 0) { _p += 1; }
                else if (nc == "." && !seenDot) { seenDot = true; _p += 1; }
                else { break; }
            }
            EmitConst(ParseUtil.ParseFloat(_s.Substring(start, _p - start), 0.0f));
            return;
        }
        int idStart = _p;
        while (_p < _s.Length) {
            string ic = _s.Substring(_p, 1);
            if (ic == "(" || ic == ")" || ic == "," || ic == "+" || ic == "-" ||
                ic == "*" || ic == "/" || ic == "%" || ic == "^" || ic == " ") { break; }
            _p += 1;
        }
        string ident = _s.Substring(idStart, _p - idStart).ToLower();
        if (ident == "x" || ident == "value") {
            Emit(2, 0);
            return;
        }
        if (ident == "pi") {
            EmitConst(3.14159265f);
            return;
        }
        if (Peek() == "(") {
            _p += 1;
            CompileExpr();
            int argc = 1;
            if (Peek() == ",") {
                _p += 1;
                if (ident == "min" || ident == "max" || ident == "pow" || ident == "clamp") {
                    CompileExpr();
                    argc = 2;
                } else {
                    var discard = new MathExprParser("");
                    discard._s = _s;
                    discard._p = _p;
                    discard.CompileExpr();
                    _p = discard._p;
                }
            }
            if (Peek() == ",") {
                _p += 1;
                if (ident == "clamp") {
                    CompileExpr();
                    argc = 3;
                } else {
                    var discard2 = new MathExprParser("");
                    discard2._s = _s;
                    discard2._p = _p;
                    discard2.CompileExpr();
                    _p = discard2._p;
                }
            }
            if (Peek() == ")") { _p += 1; }
            if (ident == "floor") { Emit(10, 0); return; }
            if (ident == "ceil") { Emit(11, 0); return; }
            if (ident == "round") { Emit(12, 0); return; }
            if (ident == "abs") { Emit(13, 0); return; }
            if (ident == "sqrt") { Emit(14, 0); return; }
            if (ident == "sin") { Emit(15, 0); return; }
            if (ident == "cos") { Emit(16, 0); return; }
            if (ident == "min" && argc >= 2) { Emit(17, 0); return; }
            if (ident == "max" && argc >= 2) { Emit(18, 0); return; }
            if (ident == "pow" && argc >= 2) { Emit(8, 0); return; }
            if (ident == "clamp" && argc >= 3) { Emit(19, 0); return; }
            if (argc == 2) { _ops.Push(0); _opArgs.Push(0); _depth -= 1; }
            else if (argc == 3) { _ops.Push(0); _opArgs.Push(0); _ops.Push(0); _opArgs.Push(0); _depth -= 2; }
            return;
        }
        EmitConst(0.0f);
    }
}

public class MathConverter : DependencyObject, IValueConverter, IMultiValueConverter {
    private string _expression = "";
    private bool _cachedExprValid = false;
    private string _cachedRawExpr = "";
    private CompiledMathExpr _cachedCompiled;
    private static Dictionary<string, CompiledMathExpr> _compiledCache;

    public static int DebugCompileCount() { return MathExprParser.DebugCompileCount(); }
    public static void DebugResetCompileCount() { MathExprParser.DebugResetCompileCount(); }

    public string Expression {
        get { return _expression; }
        set {
            string next = value == null ? "" : value;
            if (_expression != next) {
                _expression = next;
                _cachedExprValid = false;
                _cachedCompiled = null;
            }
        }
    }

    public MathConverter() {}
    public MathConverter(string expression) {
        Expression = expression == null ? "" : expression;
    }

    private static string NormalizeExprString(string raw) {
        string expr = raw != null ? raw.Trim() : "";
        if (expr.Length >= 2 && expr.Substring(0, 2) == "{}") {
            expr = expr.Substring(2).Trim();
        }
        if (expr.Length > 0) {
            string first = expr.Substring(0, 1);
            if (first == "+" || first == "*" || first == "/" || first == "%") {
                expr = "{0}" + expr;
            } else if (first == "-" && expr.Length > 1 && ParseUtil.DigitValue(expr.Substring(1, 1)) >= 0) {
                expr = "{0}" + expr;
            }
        }
        return expr;
    }

    private static CompiledMathExpr GetOrCompile(string normExpr) {
        if (normExpr.Length == 0) { return null; }
        if (_compiledCache == null) { _compiledCache = new Dictionary<string, CompiledMathExpr>(); }
        if (_compiledCache.ContainsKey(normExpr)) {
            return _compiledCache[normExpr];
        }
        CompiledMathExpr compiled = MathExprParser.Compile(normExpr);
        if (_compiledCache.Count >= 256) { _compiledCache.Clear(); }
        _compiledCache[normExpr] = compiled;
        return compiled;
    }

    private CompiledMathExpr ResolveCompiled(object parameter, ref string outNormExpr) {
        if (_expression.Length > 0) {
            if (_cachedExprValid) {
                return _cachedCompiled;
            }
            string norm = NormalizeExprString(_expression);
            _cachedRawExpr = _expression;
            _cachedCompiled = GetOrCompile(norm);
            _cachedExprValid = true;
            outNormExpr = norm;
            return _cachedCompiled;
        }
        string raw = (parameter is DependencyObject)
            ? DependencyObject.UnboxString(parameter as DependencyObject, "")
            : "";
        if (raw.Length == 0) { return null; }
        if (_cachedExprValid && raw == _cachedRawExpr) {
            return _cachedCompiled;
        }
        string normParam = NormalizeExprString(raw);
        _cachedRawExpr = raw;
        _cachedCompiled = GetOrCompile(normParam);
        _cachedExprValid = true;
        outNormExpr = normParam;
        return _cachedCompiled;
    }

    private string ResolveExprString(object parameter) {
        string raw = _expression.Length > 0
            ? _expression
            : ((parameter is DependencyObject) ? DependencyObject.UnboxString(parameter as DependencyObject, "") : "");
        return NormalizeExprString(raw);
    }

    public virtual object Convert(object value, string targetType, object parameter, string culture) {
        float x = (value is DependencyObject) ? DependencyObject.UnboxFloat(value as DependencyObject, 0.0f) : 0.0f;
        string unusedNorm = "";
        CompiledMathExpr compiled = ResolveCompiled(parameter, ref unusedNorm);
        if (compiled == null) { return DependencyObject.BoxFloat(x); }
        return DependencyObject.BoxFloat(compiled.EvalSingle(x));
    }

    public virtual object Convert(DependencyObject[] values, string targetType, object parameter, string culture) {
        int n = values == null ? 0 : values.Length;
        string unusedNorm = "";
        CompiledMathExpr compiled = ResolveCompiled(parameter, ref unusedNorm);
        if (compiled == null) {
            float first = (n > 0 && values[0] != null) ? DependencyObject.UnboxFloat(values[0], 0.0f) : 0.0f;
            return DependencyObject.BoxFloat(first);
        }
        return DependencyObject.BoxFloat(compiled.EvalMulti(values));
    }

    public virtual object ConvertBack(object value, string targetType, object parameter, string culture) {
        float y = (value is DependencyObject) ? DependencyObject.UnboxFloat(value as DependencyObject, 0.0f) : 0.0f;
        string expr = ResolveExprString(parameter);
        if (expr.Length == 0) { return DependencyObject.BoxFloat(y); }
        if (expr.Length >= 3 && expr.Substring(0, 3) == "{0}") {
            string tail = expr.Substring(3).Trim();
            if (tail.Length > 1) {
                string op = tail.Substring(0, 1);
                float k = ParseUtil.ParseFloat(tail.Substring(1), 0.0f);
                if (op == "+") { return DependencyObject.BoxFloat(y - k); }
                if (op == "-") { return DependencyObject.BoxFloat(y + k); }
                if (op == "*") { return DependencyObject.BoxFloat(k != 0.0f ? (y / k) : 0.0f); }
                if (op == "/") { return DependencyObject.BoxFloat(y * k); }
            }
        }
        return DependencyObject.BoxFloat(y);
    }
}
