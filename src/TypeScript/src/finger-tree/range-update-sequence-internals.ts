/**
 * Node-level internals of the lazily range-updating sequence, plus its diagnostic counters.
 *
 * Separated from the public surface so the tree mechanics - tag pushdown, rebalancing, and cached-
 * measure maintenance - can be read on their own, and so the tests can assert callback budgets.
 */
/** Test-facing counters for one synchronous range-update sequence operation. */
export interface RangeUpdateOperationStatistics {
    /** How many nodes the operation visited. */
    readonly nodeVisits: number;
    /** How many nodes the operation allocated. */
    readonly nodeAllocations: number;
    /** How many public wrapper objects the operation allocated. */
    readonly facadeAllocations: number;
    /** How many rebalancing rotations the operation performed. */
    readonly rotations: number;
    /** How many pending tags the operation pushed down. */
    readonly pushes: number;
    /** How many covered subtrees the operation tagged rather than descending into. */
    readonly subtreeApplications: number;
    /** How many element-measure callbacks the operation made. */
    readonly elementMeasureCallbacks: number;
    /** How many measure-combine callbacks the operation made. */
    readonly measureCombineCallbacks: number;
    /** How many identity-test callbacks the operation made. */
    readonly identityTestCallbacks: number;
    /** How many tag-composition callbacks the operation made. */
    readonly tagComposeCallbacks: number;
    /** How many element-apply callbacks the operation made. */
    readonly elementApplyCallbacks: number;
    /** How many measure-apply callbacks the operation made. */
    readonly measureApplyCallbacks: number;
    /**
     * Total caller-supplied policy invocations, summed across all six callback kinds, so a test can
     * assert an operation stays within its documented budget.
     */
    readonly policyCallbacks: number;
}

/** A synchronous observation scope. Scopes must be disposed in stack order. */
export interface RangeUpdateObservation {
    /** The counters accumulated so far. */
    readonly statistics: RangeUpdateOperationStatistics;
    /** End the observation, restoring the enclosing one. */
    dispose(): void;
}

interface MutableStatistics {
    nodeVisits: number;
    nodeAllocations: number;
    facadeAllocations: number;
    rotations: number;
    pushes: number;
    subtreeApplications: number;
    elementMeasureCallbacks: number;
    measureCombineCallbacks: number;
    identityTestCallbacks: number;
    tagComposeCallbacks: number;
    elementApplyCallbacks: number;
    measureApplyCallbacks: number;
}

class Observation implements RangeUpdateObservation {
    public readonly counters: MutableStatistics = {
        nodeVisits: 0,
        nodeAllocations: 0,
        facadeAllocations: 0,
        rotations: 0,
        pushes: 0,
        subtreeApplications: 0,
        elementMeasureCallbacks: 0,
        measureCombineCallbacks: 0,
        identityTestCallbacks: 0,
        tagComposeCallbacks: 0,
        elementApplyCallbacks: 0,
        measureApplyCallbacks: 0,
    };

    readonly #previous: Observation | undefined;
    #disposed: boolean = false;

    public constructor(previous: Observation | undefined) {
        this.#previous = previous;
    }

    public get statistics(): RangeUpdateOperationStatistics {
        const counters = this.counters;
        return {
            ...counters,
            policyCallbacks:
                counters.elementMeasureCallbacks
                + counters.measureCombineCallbacks
                + counters.identityTestCallbacks
                + counters.tagComposeCallbacks
                + counters.elementApplyCallbacks
                + counters.measureApplyCallbacks,
        };
    }

    public dispose(): void {
        if (this.#disposed) return;
        if (currentObservation !== this) {
            throw new Error("Range-update observation scopes must be disposed in stack order.");
        }
        currentObservation = this.#previous;
        this.#disposed = true;
    }
}

let currentObservation: Observation | undefined;

/** Begins a synchronous diagnostic observation scope. */
export function beginRangeUpdateObservation(): RangeUpdateObservation {
    const observation = new Observation(currentObservation);
    currentObservation = observation;
    return observation;
}

/** Observes a successful synchronous operation and disposes the scope in all cases. */
export function observeRangeUpdateOperation<Result>(
    operation: () => Result,
): { readonly result: Result; readonly statistics: RangeUpdateOperationStatistics } {
    const observation = beginRangeUpdateObservation();
    try {
        const result = operation();
        return { result, statistics: observation.statistics };
    } finally {
        observation.dispose();
    }
}

/** Count one node visit against the active observation. */
export function recordRangeUpdateNodeVisit(): void {
    if (currentObservation !== undefined) currentObservation.counters.nodeVisits++;
}
/** Count one node allocation against the active observation. */
export function recordRangeUpdateNodeAllocation(): void {
    if (currentObservation !== undefined) currentObservation.counters.nodeAllocations++;
}
/** Count one wrapper allocation against the active observation. */
export function recordRangeUpdateFacadeAllocation(): void {
    if (currentObservation !== undefined) currentObservation.counters.facadeAllocations++;
}
/** Count one rebalancing rotation against the active observation. */
export function recordRangeUpdateRotation(): void {
    if (currentObservation !== undefined) currentObservation.counters.rotations++;
}
/** Count one tag pushdown against the active observation. */
export function recordRangeUpdatePush(): void {
    if (currentObservation !== undefined) currentObservation.counters.pushes++;
}
/** Count one whole-subtree tag application against the active observation. */
export function recordRangeUpdateSubtreeApplication(): void {
    if (currentObservation !== undefined) currentObservation.counters.subtreeApplications++;
}
/** Count one element-measure callback against the active observation. */
export function recordRangeUpdateElementMeasureCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.elementMeasureCallbacks++;
}
/** Count one measure-combine callback against the active observation. */
export function recordRangeUpdateMeasureCombineCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.measureCombineCallbacks++;
}
/** Count one identity-test callback against the active observation. */
export function recordRangeUpdateIdentityTestCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.identityTestCallbacks++;
}
/** Count one tag-composition callback against the active observation. */
export function recordRangeUpdateTagComposeCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.tagComposeCallbacks++;
}
/** Count one element-apply callback against the active observation. */
export function recordRangeUpdateElementApplyCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.elementApplyCallbacks++;
}
/** Count one measure-apply callback against the active observation. */
export function recordRangeUpdateMeasureApplyCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.measureApplyCallbacks++;
}
