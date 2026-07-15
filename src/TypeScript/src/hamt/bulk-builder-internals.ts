type UntypedCombiner = (existing: unknown, incoming: unknown) => unknown;

const combiners = new WeakMap<object, UntypedCombiner>();

/** Attaches a package-internal duplicate combiner without widening the public builder surface. */
export function attachBulkBuilderCombiner<V>(
    builder: object,
    combine: (existing: V, incoming: V) => V,
): void {
    combiners.set(builder, combine as UntypedCombiner);
}

/** Gets the package-internal duplicate combiner attached to a construction builder. */
export function getBulkBuilderCombiner<V>(
    builder: object,
): ((existing: V, incoming: V) => V) | undefined {
    return combiners.get(builder) as ((existing: V, incoming: V) => V) | undefined;
}
