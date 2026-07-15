/** Test-facing counters for one synchronous range-update sequence operation. */
export interface RangeUpdateOperationStatistics {
    readonly nodeVisits: number;
    readonly nodeAllocations: number;
    readonly facadeAllocations: number;
    readonly rotations: number;
    readonly pushes: number;
    readonly subtreeApplications: number;
    readonly elementMeasureCallbacks: number;
    readonly measureCombineCallbacks: number;
    readonly identityTestCallbacks: number;
    readonly tagComposeCallbacks: number;
    readonly elementApplyCallbacks: number;
    readonly measureApplyCallbacks: number;
    readonly policyCallbacks: number;
}

/** A synchronous observation scope. Scopes must be disposed in stack order. */
export interface RangeUpdateObservation {
    readonly statistics: RangeUpdateOperationStatistics;
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

export function recordRangeUpdateNodeVisit(): void {
    if (currentObservation !== undefined) currentObservation.counters.nodeVisits++;
}
export function recordRangeUpdateNodeAllocation(): void {
    if (currentObservation !== undefined) currentObservation.counters.nodeAllocations++;
}
export function recordRangeUpdateFacadeAllocation(): void {
    if (currentObservation !== undefined) currentObservation.counters.facadeAllocations++;
}
export function recordRangeUpdateRotation(): void {
    if (currentObservation !== undefined) currentObservation.counters.rotations++;
}
export function recordRangeUpdatePush(): void {
    if (currentObservation !== undefined) currentObservation.counters.pushes++;
}
export function recordRangeUpdateSubtreeApplication(): void {
    if (currentObservation !== undefined) currentObservation.counters.subtreeApplications++;
}
export function recordRangeUpdateElementMeasureCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.elementMeasureCallbacks++;
}
export function recordRangeUpdateMeasureCombineCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.measureCombineCallbacks++;
}
export function recordRangeUpdateIdentityTestCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.identityTestCallbacks++;
}
export function recordRangeUpdateTagComposeCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.tagComposeCallbacks++;
}
export function recordRangeUpdateElementApplyCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.elementApplyCallbacks++;
}
export function recordRangeUpdateMeasureApplyCallback(): void {
    if (currentObservation !== undefined) currentObservation.counters.measureApplyCallbacks++;
}
