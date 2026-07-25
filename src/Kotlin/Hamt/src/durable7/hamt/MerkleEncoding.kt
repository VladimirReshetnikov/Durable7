package durable7.hamt

import java.nio.ByteBuffer
import java.nio.CharBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction
import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import java.util.UUID

/** An injective, explicitly versioned canonical value encoding. */
public interface MerkleCodec<T> {
    /** Stable identifier ending in `-v` followed by ASCII decimal digits. */
    public val encodingId: String

    /** Encodes one value into newly owned canonical bytes. */
    public fun encode(value: T): ByteArray

    /** Decodes exactly one complete canonical encoding. */
    public fun decode(encoding: ByteArray): T
}

/** A malformed, noncanonical, or otherwise rejected codec value. */
public class MerkleCodecException(message: String, cause: Throwable? = null) :
    IllegalArgumentException(message, cause)

/** Canonical signed 32-bit big-endian codec (`i32-be-v1`). */
public object Int32MerkleCodec : MerkleCodec<Int> {
    override val encodingId: String = "i32-be-v1"

    override fun encode(value: Int): ByteArray = byteArrayOf(
        (value ushr 24).toByte(),
        (value ushr 16).toByte(),
        (value ushr 8).toByte(),
        value.toByte(),
    )

    override fun decode(encoding: ByteArray): Int {
        if (encoding.size != Int.SIZE_BYTES) {
            throw MerkleCodecException("Invalid $encodingId encoding: expected exactly four bytes.")
        }
        return (encoding[0].toInt() and 0xff shl 24) or
            (encoding[1].toInt() and 0xff shl 16) or
            (encoding[2].toInt() and 0xff shl 8) or
            (encoding[3].toInt() and 0xff)
    }
}

/** Canonical signed 64-bit big-endian codec (`i64-be-v1`). */
public object Int64MerkleCodec : MerkleCodec<Long> {
    override val encodingId: String = "i64-be-v1"

    override fun encode(value: Long): ByteArray = ByteArray(Long.SIZE_BYTES) { index ->
        (value ushr (56 - index * 8)).toByte()
    }

    override fun decode(encoding: ByteArray): Long {
        if (encoding.size != Long.SIZE_BYTES) {
            throw MerkleCodecException("Invalid $encodingId encoding: expected exactly eight bytes.")
        }
        var result = 0L
        for (byte in encoding) result = result shl 8 or (byte.toLong() and 0xff)
        return result
    }
}

/** Canonical nullable strict UTF-8 codec (`nullable-utf8-v1`). */
public object NullableUtf8MerkleCodec : MerkleCodec<String?> {
    override val encodingId: String = "nullable-utf8-v1"

    override fun encode(value: String?): ByteArray {
        if (value == null) return byteArrayOf(0)
        val payload = try {
            val encoder = StandardCharsets.UTF_8.newEncoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
            val buffer = encoder.encode(CharBuffer.wrap(value))
            ByteArray(buffer.remaining()).also(buffer::get)
        } catch (exception: CharacterCodingException) {
            throw MerkleCodecException("Invalid $encodingId value: input is not valid Unicode.", exception)
        }
        return ByteArray(Math.addExact(payload.size, 1)).also { result ->
            result[0] = 1
            payload.copyInto(result, 1)
        }
    }

    override fun decode(encoding: ByteArray): String? {
        if (encoding.isEmpty()) {
            throw MerkleCodecException("Invalid $encodingId encoding: nullable tag is missing.")
        }
        return when (encoding[0].toInt() and 0xff) {
            0 -> {
                if (encoding.size != 1) {
                    throw MerkleCodecException("Invalid $encodingId encoding: null has trailing bytes.")
                }
                null
            }
            1 -> try {
                val decoder = StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                decoder.decode(ByteBuffer.wrap(encoding, 1, encoding.size - 1)).toString()
            } catch (exception: CharacterCodingException) {
                throw MerkleCodecException("Invalid $encodingId encoding: payload is not canonical UTF-8.", exception)
            }
            else -> throw MerkleCodecException(
                "Invalid $encodingId encoding: nullable tag must be zero or one.",
            )
        }
    }
}

/** Canonical nullable byte-array codec (`nullable-bytes-v1`). */
public object NullableBytesMerkleCodec : MerkleCodec<ByteArray?> {
    override val encodingId: String = "nullable-bytes-v1"

    override fun encode(value: ByteArray?): ByteArray {
        if (value == null) return byteArrayOf(0)
        return ByteArray(Math.addExact(value.size, 1)).also { result ->
            result[0] = 1
            value.copyInto(result, 1)
        }
    }

    override fun decode(encoding: ByteArray): ByteArray? {
        if (encoding.isEmpty()) {
            throw MerkleCodecException("Invalid $encodingId encoding: nullable tag is missing.")
        }
        return when (encoding[0].toInt() and 0xff) {
            0 -> {
                if (encoding.size != 1) {
                    throw MerkleCodecException("Invalid $encodingId encoding: null has trailing bytes.")
                }
                null
            }
            1 -> encoding.copyOfRange(1, encoding.size)
            else -> throw MerkleCodecException(
                "Invalid $encodingId encoding: nullable tag must be zero or one.",
            )
        }
    }
}

/** Canonical RFC-4122/network-byte-order UUID codec (`guid-rfc4122-v1`). */
public object Rfc4122UuidMerkleCodec : MerkleCodec<UUID> {
    override val encodingId: String = "guid-rfc4122-v1"

    override fun encode(value: UUID): ByteArray = ByteArray(16).also { result ->
        writeLongBigEndian(result, 0, value.mostSignificantBits)
        writeLongBigEndian(result, 8, value.leastSignificantBits)
    }

    override fun decode(encoding: ByteArray): UUID {
        if (encoding.size != 16) {
            throw MerkleCodecException("Invalid $encodingId encoding: expected exactly sixteen bytes.")
        }
        return UUID(readLongBigEndian(encoding, 0), readLongBigEndian(encoding, 8))
    }
}

/** Immutable 256-bit SHA-256 content address. */
public class MerkleDigest private constructor(private val bytes: ByteArray) : Comparable<MerkleDigest> {
    public companion object {
        public const val BYTE_LENGTH: Int = 32
        public const val HEX_LENGTH: Int = BYTE_LENGTH * 2

        private val sha256: ThreadLocal<MessageDigest> = ThreadLocal.withInitial {
            MessageDigest.getInstance("SHA-256")
        }

        /** Copies exactly 32 digest bytes. */
        public fun fromBytes(bytes: ByteArray): MerkleDigest {
            require(bytes.size == BYTE_LENGTH) {
                "A Merkle digest must contain exactly $BYTE_LENGTH bytes; received ${bytes.size}."
            }
            return MerkleDigest(bytes.copyOf())
        }

        /** Parses exactly 64 hexadecimal characters, accepting either case. */
        public fun fromHex(value: String): MerkleDigest {
            require(value.length == HEX_LENGTH) {
                "A Merkle digest must contain exactly $HEX_LENGTH hexadecimal characters; received ${value.length}."
            }
            val bytes = ByteArray(BYTE_LENGTH)
            for (index in bytes.indices) {
                val high = hexValue(value[index * 2])
                val low = hexValue(value[index * 2 + 1])
                require(high >= 0) { "Invalid hexadecimal character at index ${index * 2}." }
                require(low >= 0) { "Invalid hexadecimal character at index ${index * 2 + 1}." }
                bytes[index] = (high shl 4 or low).toByte()
            }
            return MerkleDigest(bytes)
        }

        /** Computes SHA-256 over exact bytes. */
        public fun hash(bytes: ByteArray): MerkleDigest {
            val digest = sha256.get()
            digest.reset()
            return MerkleDigest(digest.digest(bytes))
        }

        internal fun hashFramed(tag: Int, fields: List<ByteArray>): MerkleDigest {
            val digest = sha256.get()
            digest.reset()
            digest.update(tag.toByte())
            for (field in fields) {
                digest.update(Int32MerkleCodec.encode(field.size))
                digest.update(field)
            }
            return MerkleDigest(digest.digest())
        }

        private fun hexValue(value: Char): Int = when (value) {
            in '0'..'9' -> value.code - '0'.code
            in 'a'..'f' -> value.code - 'a'.code + 10
            in 'A'..'F' -> value.code - 'A'.code + 10
            else -> -1
        }
    }

    /** Copies digest bytes into a newly owned array. */
    public fun toByteArray(): ByteArray = bytes.copyOf()

    /** Writes only when 32 bytes fit at [offset], leaving the destination unchanged on failure. */
    public fun tryWriteBytes(destination: ByteArray, offset: Int = 0): Boolean {
        if (offset < 0 || offset > destination.size - BYTE_LENGTH) return false
        bytes.copyInto(destination, offset)
        return true
    }

    internal fun copyInto(destination: ByteArray, offset: Int): Unit {
        bytes.copyInto(destination, offset)
    }

    internal fun byteAt(index: Int): Int = bytes[index].toInt() and 0xff

    override fun compareTo(other: MerkleDigest): Int {
        for (index in bytes.indices) {
            val comparison = byteAt(index).compareTo(other.byteAt(index))
            if (comparison != 0) return comparison
        }
        return 0
    }

    override fun equals(other: Any?): Boolean = other is MerkleDigest && bytes.contentEquals(other.bytes)

    override fun hashCode(): Int = bytes.contentHashCode()

    override fun toString(): String = buildString(HEX_LENGTH) {
        for (byte in bytes) append("%02x".format(byte.toInt() and 0xff))
    }
}

/** Deterministic comparison, encoding, and hash domain for one Merkle search-tree family. */
public class MerkleSearchTreePolicy<K, V> private constructor(
    public val policyId: String,
    public val comparator: Comparator<K>,
    public val keyCodec: MerkleCodec<K>,
    public val valueCodec: MerkleCodec<V>,
    public val domainDigest: MerkleDigest,
    /** Canonical empty-manifest content address for this policy domain. */
    public val emptyDigest: MerkleDigest,
) {
    public companion object {
        public const val ALGORITHM_ID: String = "mst-sha256-b16-v2"

        /** Creates an explicit deterministic policy. */
        public fun <K, V> create(
            policyId: String,
            comparator: Comparator<K>,
            keyCodec: MerkleCodec<K>,
            valueCodec: MerkleCodec<V>,
        ): MerkleSearchTreePolicy<K, V> {
            require(policyId.isNotBlank()) { "A Merkle policy ID must not be blank." }
            validateEncodingId(keyCodec.encodingId, "key codec")
            validateEncodingId(valueCodec.encodingId, "value codec")
            val domain = MerkleDigest.hashFramed(
                0x50,
                listOf(
                    strictUtf8(ALGORITHM_ID, "algorithm ID"),
                    strictUtf8(policyId, "policy ID"),
                    strictUtf8(keyCodec.encodingId, "key codec ID"),
                    strictUtf8(valueCodec.encodingId, "value codec ID"),
                ),
            )
            val emptyManifest = ByteArray(5 + MerkleDigest.BYTE_LENGTH)
            "MST2".toByteArray(StandardCharsets.US_ASCII).copyInto(emptyManifest)
            emptyManifest[4] = 0
            check(domain.tryWriteBytes(emptyManifest, 5))
            return MerkleSearchTreePolicy(
                policyId,
                comparator,
                keyCodec,
                valueCodec,
                domain,
                MerkleDigest.hash(emptyManifest),
            )
        }

        /** Creates a natural-order deterministic policy. */
        public fun <K : Comparable<K>, V> natural(
            policyId: String,
            keyCodec: MerkleCodec<K>,
            valueCodec: MerkleCodec<V>,
        ): MerkleSearchTreePolicy<K, V> = create(
            policyId,
            Comparator { left, right -> left.compareTo(right) },
            keyCodec,
            valueCodec,
        )

        /** Counts leading zero base-16 digits, yielding a level from zero through 64. */
        public fun level(digest: MerkleDigest): Int {
            var result = 0
            for (index in 0 until MerkleDigest.BYTE_LENGTH) {
                val byte = digest.byteAt(index)
                if (byte and 0xf0 != 0) return result
                result++
                if (byte and 0x0f != 0) return result
                result++
            }
            return result
        }

        private fun validateEncodingId(value: String, field: String): Unit {
            val marker = value.lastIndexOf("-v")
            val valid = value.isNotEmpty() && value.trim() == value && marker > 0 &&
                marker + 2 < value.length && value.substring(marker + 2).all { it in '0'..'9' }
            require(valid) {
                "Invalid $field encoding ID '$value': expected an unpadded ID ending in -v<digits>."
            }
            strictUtf8(value, "$field encoding ID")
        }

        private fun strictUtf8(value: String, field: String): ByteArray = try {
            val encoder = StandardCharsets.UTF_8.newEncoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
            val buffer = encoder.encode(CharBuffer.wrap(value))
            ByteArray(buffer.remaining()).also(buffer::get)
        } catch (exception: CharacterCodingException) {
            throw IllegalArgumentException("The Merkle $field is not valid Unicode.", exception)
        }
    }

    /** Returns whether algorithm, semantic policy, and both codec domains agree. */
    public fun isCompatibleWith(other: MerkleSearchTreePolicy<K, V>): Boolean =
        domainDigest == other.domainDigest

    /** Computes the policy-bound key digest from canonical encoded bytes. */
    public fun hashKeyBytes(keyBytes: ByteArray): MerkleDigest = MerkleDigest.hashFramed(
        0x4b,
        listOf(domainDigest.toByteArray(), keyBytes),
    )

    /** Canonically encodes and hashes one key. */
    public fun hashKey(key: K): MerkleDigest = hashKeyBytes(keyCodec.encode(key))

    /** Computes SHA-256 over exact bytes. */
    public fun hashBytes(bytes: ByteArray): MerkleDigest = MerkleDigest.hash(bytes)
}

private fun writeLongBigEndian(destination: ByteArray, offset: Int, value: Long): Unit {
    for (index in 0 until Long.SIZE_BYTES) {
        destination[offset + index] = (value ushr (56 - index * 8)).toByte()
    }
}

private fun readLongBigEndian(source: ByteArray, offset: Int): Long {
    var result = 0L
    for (index in 0 until Long.SIZE_BYTES) {
        result = result shl 8 or (source[offset + index].toLong() and 0xff)
    }
    return result
}
