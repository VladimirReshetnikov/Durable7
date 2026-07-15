import type { MeasurePolicy } from "./measures.js";

/**
 * Ordered sequence measurement together with a monoid of lazy range-update tags.
 *
 * `compose(newer, older)` is directional: it represents applying `older` first and
 * `newer` second. Implementations must make tags a monoid, make the tag action agree on
 * elements and singleton measures, and distribute that action over ordered measure
 * combination. Every value accepted by `isIdentity` must have full identity behavior;
 * identity recognition is semantic and is not based on JavaScript value equality.
 */
export interface RangeUpdateAlgebra<Element, Measure, Tag>
    extends MeasurePolicy<Element, Measure> {
    /** The tag-monoid identity. This value is never used as an absence sentinel. */
    readonly identityTag: Tag;

    /** Returns whether a tag has algebraic identity behavior. */
    isIdentity(tag: Tag): boolean;

    /** Returns the tag equivalent to applying `older` and then `newer`. */
    compose(newer: Tag, older: Tag): Tag;

    /** Applies a tag to one current logical element. */
    applyElement(tag: Tag, element: Element): Element;

    /** Applies a tag to the measure of `count` current logical elements. */
    applyMeasure(tag: Tag, measure: Measure, count: number): Measure;
}
