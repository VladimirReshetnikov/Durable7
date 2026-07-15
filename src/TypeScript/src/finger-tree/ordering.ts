/** Total-order comparison function, following Array.sort's negative/zero/positive convention. */
export type Comparator<T> = (left: T, right: T) => number;

/** Natural JavaScript relational ordering for mutually comparable values. */
export function defaultComparator<T>(left: T, right: T): number {
    if (left === right) return 0;
    return left < right ? -1 : 1;
}

/** Returns a comparator with the opposite order. */
export function reverseComparator<T>(comparator: Comparator<T>): Comparator<T> {
    return (left: T, right: T): number => comparator(right, left);
}
