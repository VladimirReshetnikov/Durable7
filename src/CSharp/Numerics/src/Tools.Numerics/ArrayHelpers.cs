// SPDX-License-Identifier: MIT

using System;

namespace Tools.Numerics
{
    /// <summary>
    /// Contains utility methods for array manipulation.
    /// </summary>
    internal static class ArrayHelpers
    {
        public static T[]? RemoveElement<T>(T[]? array, T item, out bool removed) where T : IComparable<T>
        {
            if (array == null || array.Length == 0)
            {
                removed = false;
                return null;
            }

            var index = Array.BinarySearch(array, item);
            if (index < 0)
            {
                removed = false;
                return array;
            }

            removed = true;

            if (array.Length == 1)
            {
                return null;
            }

            var newArray = new T[array.Length - 1];

            if (index > 0)
            {
                Array.Copy(array, 0, newArray, 0, index);
            }

            if (index < array.Length - 1)
            {
                Array.Copy(array, index + 1, newArray, index, array.Length - 1 - index);
            }

            return newArray;
        }

        public static T[] InsertElement<T>(T[]? array, T item) where T : IComparable<T>
        {
            if (array == null || array.Length == 0)
            {
                return new[] {item};
            }

            var index = ~Array.BinarySearch(array, item);
            if (index < 0)
            {
                throw new InvalidOperationException("The item already exists.");
            }

            var newArray = new T[array.Length + 1];

            if (index > 0)
            {
                Array.Copy(array, 0, newArray, 0, index);
            }

            newArray[index] = item;

            if (index < newArray.Length - 1)
            {
                Array.Copy(array, index, newArray, index + 1, array.Length - index);
            }

            return newArray;
        }
    }
}
