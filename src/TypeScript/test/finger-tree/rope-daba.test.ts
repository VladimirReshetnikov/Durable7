/**
 * Tests for the rope layers and the DABA Lite sliding-window aggregate, including chunk handling,
 * text line navigation, and worst-case callback bounds.
 */
import { describe, expect, test } from "vitest";
import {
    DabaLite,
    NumberSumMeasure,
    MeasuredRope,
    Rope,
    RopeBuilder,
    TextRope,
    TextRopeCursor,
    type Monoid,
} from "../../src/finger-tree/index.js";

describe("rope family", () => {
    test("positional and measured edits preserve snapshots", () => {
        const basis = Rope.from([1, 2, 3, 4]);
        const edited = basis.insertRange(2, [8, 9])!.removeRange(1, 2)!;
        expect(basis.toArray()).toEqual([1, 2, 3, 4]);
        expect(edited.toArray()).toEqual([1, 9, 3, 4]);

        const measure = new NumberSumMeasure();
        const measured = MeasuredRope.from([2, 3, 5, 7], measure);
        expect(measured.measure()).toBe(17);
        expect(measured.locateByMeasure((sum) => sum >= 8)).toEqual({ index: 2, measureBefore: 5, value: 5, found: true });
        expect(measured.slice(1, 2)!.measure()).toBe(8);
    });

    test("text rope maps UTF-16 offsets and lines", () => {
        const text = TextRope.fromText("alpha\nbeta\n🙂x");
        expect(text.lineCount()).toBe(3);
        expect(text.lines()).toEqual(["alpha", "beta", "🙂x"]);
        expect(text.lineColumnOf(8)).toEqual({ line: 1, column: 2 });
        expect(text.offsetOf(2, 2)).toBe(13);
        expect(text.size).toBe(14);
    });

    test("builder publishes isolated snapshots", () => {
        const builder = new RopeBuilder().append("a").appendLine("b");
        const first = builder.toTextRope();
        builder.append("c");
        expect(first.asString()).toBe("ab\n");
        expect(builder.asString()).toBe("ab\nc");
    });

    test("persistent gap cursors branch from retained versions", () => {
        const source = Rope.fromText("abcdef");
        const clean = source.getCursor(3);
        expect(clean.snapshot()).toBe(source);
        expect([clean.peekPrevious(), clean.peekNext()]).toEqual(["c", "d"]);
        const left = clean.insertRange("XY".split("")).deleteNext();
        const right = clean.deletePrevious().replaceNext("Z");
        expect(left.snapshot().toArray().join("")).toBe("abcXYef");
        expect(right.snapshot().toArray().join("")).toBe("abZef");
        expect(source.toArray().join("")).toBe("abcdef");
    });

    test("cursor entry peeks distinguish stored undefined from boundaries", () => {
        const positional = Rope.from<number | undefined>([undefined]).getCursor();
        expect(positional.peekPreviousEntry()).toBeUndefined();
        expect(positional.peekNext()).toBeUndefined();
        expect(positional.peekNextEntry()).toEqual({ value: undefined });
        expect(positional.moveNext().peekPreviousEntry()).toEqual({ value: undefined });

        const policy = {
            identity: 0,
            combine: (left: number, right: number): number => left + right,
            measure: (_value: number | undefined): number => 1,
        };
        const measured = MeasuredRope.from<number | undefined, number>([undefined], policy).getCursor();
        expect(measured.peekPreviousEntry()).toBeUndefined();
        expect(measured.peekNext()).toBeUndefined();
        expect(measured.peekNextEntry()).toEqual({ value: undefined });
        expect(measured.moveNext().peekPreviousEntry()).toEqual({ value: undefined });
    });

    test("cursor replacement always publishes a fresh edit without equality shortcuts", () => {
        const value = {};
        const positional = Rope.from([value]);
        const positionalEdit = positional.getCursor().replaceNext(value).snapshot();
        expect(positionalEdit).not.toBe(positional);
        expect(positionalEdit.get(0)).toBe(value);

        let measureCalls = 0;
        const policy = {
            identity: 0,
            combine: (left: number, right: number): number => left + right,
            measure: (_value: object): number => { measureCalls++; return 1; },
        };
        const measured = MeasuredRope.from([value], policy);
        measureCalls = 0;
        const measuredEdit = measured.getCursor().replaceNext(value).snapshot();
        expect(measuredEdit).not.toBe(measured);
        expect(measuredEdit.get(0)).toBe(value);
        expect(measureCalls).toBeGreaterThan(0);
    });

    test("measured cursors retain ordered prefix and suffix aggregates", () => {
        const rope = MeasuredRope.from([2, 3, 5, 7], new NumberSumMeasure());
        const cursor = rope.getCursor(2);
        expect([cursor.measureBefore, cursor.measureAfter]).toEqual([5, 12]);
        const edited = cursor.insert(11).deletePrevious();
        expect(edited.position).toBe(2);
        expect(edited.snapshot().toArray()).toEqual([2, 3, 5, 7]);
        expect(rope.getCursorByMeasure(sum => sum >= 8)?.position).toBe(2);
    });

    test("text cursors navigate line/column and preserve old text", () => {
        const source = TextRope.fromText("alpha\nbeta");
        const cursor = source.getCursor(6);
        expect([cursor.line, cursor.column]).toEqual([1, 0]);
        const edited = cursor.insert("new\n").seekLineColumn(2, 2).insert("!");
        expect(edited.snapshot().asString()).toBe("alpha\nnew\nbe!ta");
        expect(source.asString()).toBe("alpha\nbeta");
    });

    test("text cursor snapshots wrap the edited version instead of rebuilding it", () => {
        const source = TextRope.fromText("alpha\nbeta\ngamma");
        const cursor = source.getCursor(3);
        expect(cursor.snapshot()).toBe(source);
        expect(cursor.insert("")).toBe(cursor);
        expect(cursor.insert("").snapshot()).toBe(source);

        const edited = cursor.insert("XY");
        expect(edited.snapshot().asString()).toBe("alpXYha\nbeta\ngamma");
        expect(edited.snapshot()).toBe(edited.snapshot());
        expect(edited.snapshot().toMeasuredRope()).toBe(edited.cursor.snapshot());
        expect(edited.snapshot().toMeasuredRope().sharesStorageWith(source.toMeasuredRope())).toBe(true);
        expect(source.asString()).toBe("alpha\nbeta\ngamma");
    });

    test("text rope wrapping requires the shared newline measure policy", () => {
        const equivalent = {
            identity: 0,
            combine: (left: number, right: number): number => left + right,
            measure: (character: string): number => (character === "\n" ? 1 : 0),
        };
        expect(() => TextRope.fromMeasuredRope(MeasuredRope.from(["a", "\n"], equivalent))).toThrow(TypeError);
        const native = TextRope.fromText("a\n").toMeasuredRope();
        expect(TextRope.fromMeasuredRope(native).toMeasuredRope()).toBe(native);
    });

    test("text cursor rejects a snapshot taken from a different document", () => {
        const first = TextRope.fromText("alpha");
        const second = TextRope.fromText("gamma");
        expect(() => new TextRopeCursor(second.toMeasuredRope().getCursor(1), first)).toThrow(TypeError);
        expect(new TextRopeCursor(first.toMeasuredRope().getCursor(1), first).snapshot()).toBe(first);
        expect(new TextRopeCursor(second.toMeasuredRope().getCursor(1)).snapshot().asString()).toBe("gamma");
    });

    test("rope ranges refuse a bare string so astral text cannot enter as one element", () => {
        const clef = "\u{1D11E}";
        const rope = Rope.fromText("ab");
        expect(() => rope.getCursor(1).insertRange(clef)).toThrow(TypeError);
        expect(() => rope.insertRange(1, clef)).toThrow(TypeError);

        const inserted = rope.getCursor(1).insertRange(clef.split(""));
        expect(inserted.snapshot().size).toBe(4);
        expect(inserted.position).toBe(3);
        expect(inserted.snapshot().toArray().join("")).toBe(`a${clef}b`);

        const policy = { identity: 0, combine: (left: number, right: number): number => left + right, measure: (_value: string): number => 1 };
        expect(() => MeasuredRope.from(["a", "b"], policy).getCursor(1).insertRange(clef)).toThrow(TypeError);

        const text = TextRope.fromText("ab").getCursor(1).insert(clef);
        expect(text.snapshot().size).toBe(4);
        expect(text.snapshot().asString()).toBe(`a${clef}b`);
    });

    test("rope cursors spell length size and retain count as an alias", () => {
        const positional = Rope.fromText("abc").getCursor(1);
        expect([positional.size, positional.count]).toEqual([3, 3]);
        const measured = MeasuredRope.from([2, 3, 5], new NumberSumMeasure()).getCursor(1);
        expect([measured.size, measured.count]).toEqual([3, 3]);
        const text = TextRope.fromText("abc").getCursor(1);
        expect([text.size, text.count]).toEqual([3, 3]);
    });
});

describe("DabaLite", () => {
    test("tracks a noncommutative FIFO model through chunk churn", () => {
        const monoid: Monoid<string> = { identity: "", combine: (left, right) => left + right };
        const daba = new DabaLite(monoid);
        const model: string[] = [];
        for (let index = 0; index < 10_000; index++) {
            if (index % 3 === 0 && model.length !== 0) { daba.evict(); model.shift(); }
            else { const value = String.fromCharCode(65 + index % 26); daba.insert(value); model.push(value); }
            expect(daba.aggregate).toBe(model.join(""));
            if (index % 101 === 0) expect(daba.validateStructure().count).toBe(model.length);
        }
    });

    test("mutation callbacks are failure atomic", () => {
        let fail = false;
        const monoid: Monoid<number> = {
            identity: 0,
            combine: (left, right) => { if (fail) throw new Error("boom"); return left + right; },
        };
        const daba = new DabaLite(monoid);
        daba.insert(1); daba.insert(2);
        fail = true;
        expect(() => daba.insert(3)).toThrow("boom");
        fail = false;
        expect(daba.count).toBe(2);
        expect(daba.aggregate).toBe(3);
        expect(daba.validateStructure().count).toBe(2);
    });
});
