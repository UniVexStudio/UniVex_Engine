// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "matrix3x3.h"

#include <cmath>
#include <utility>

namespace UVE::Math {

Matrix3x3 operator*(const Matrix3x3& lhs, const Matrix3x3& rhs) noexcept {
    Matrix3x3 result{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) {
                sum += static_cast<double>(lhs.m[row][k]) * static_cast<double>(rhs.m[k][col]);
            }
            result.m[row][col] = static_cast<float>(sum);
        }
    }
    return result;
}

Matrix3x3 Transpose(const Matrix3x3& matrix) noexcept {
    Matrix3x3 result{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result.m[row][col] = matrix.m[col][row];
        }
    }
    return result;
}

bool TryInverse(const Matrix3x3& matrix, Matrix3x3& outInverse) noexcept {
    // Gauss-Jordan elimination with partial pivoting on an augmented [matrix | identity] in
    // double precision — a line-for-line 3x3 port of the Matrix4x4 overload, so the two stay
    // behaviorally identical (same singular detection, same rounding character).
    double augmented[3][6];
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const double value = static_cast<double>(matrix.m[row][col]);
            if (!std::isfinite(value)) {
                return false;
            }
            augmented[row][col] = value;
            augmented[row][col + 3] = (row == col) ? 1.0 : 0.0;
        }
    }

    constexpr double kMinimumPivot = 1e-9;
    for (int pivotIndex = 0; pivotIndex < 3; ++pivotIndex) {
        int pivotRow = pivotIndex;
        double pivotMagnitude = std::abs(augmented[pivotIndex][pivotIndex]);
        for (int row = pivotIndex + 1; row < 3; ++row) {
            const double magnitude = std::abs(augmented[row][pivotIndex]);
            if (magnitude > pivotMagnitude) {
                pivotMagnitude = magnitude;
                pivotRow = row;
            }
        }
        if (pivotMagnitude < kMinimumPivot) {
            return false; // Singular (or numerically indistinguishable from singular).
        }
        if (pivotRow != pivotIndex) {
            for (int col = 0; col < 6; ++col) {
                std::swap(augmented[pivotIndex][col], augmented[pivotRow][col]);
            }
        }

        const double pivot = augmented[pivotIndex][pivotIndex];
        for (int col = 0; col < 6; ++col) {
            augmented[pivotIndex][col] /= pivot;
        }
        for (int row = 0; row < 3; ++row) {
            if (row == pivotIndex) {
                continue;
            }
            const double factor = augmented[row][pivotIndex];
            if (factor == 0.0) {
                continue;
            }
            for (int col = 0; col < 6; ++col) {
                augmented[row][col] -= factor * augmented[pivotIndex][col];
            }
        }
    }

    Matrix3x3 result{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const double value = augmented[row][col + 3];
            if (!std::isfinite(value)) {
                return false;
            }
            result.m[row][col] = static_cast<float>(value);
        }
    }
    outInverse = result;
    return true;
}

Matrix3x3 ToMatrix3x3(const Matrix4x4& matrix) noexcept {
    Matrix3x3 result{};
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result.m[row][col] = matrix.m[row][col];
        }
    }
    return result;
}

Matrix4x4 ToMatrix4x4(const Matrix3x3& matrix) noexcept {
    Matrix4x4 result = Matrix4x4::Identity();
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            result.m[row][col] = matrix.m[row][col];
        }
    }
    return result;
}

std::string ToString(const Matrix3x3& matrix) {
    std::string result = "(";
    for (int row = 0; row < 3; ++row) {
        result += "(";
        for (int col = 0; col < 3; ++col) {
            result += std::to_string(matrix.m[row][col]);
            if (col < 2) {
                result += ", ";
            }
        }
        result += ")";
        if (row < 2) {
            result += "; ";
        }
    }
    result += ")";
    return result;
}

} // namespace UVE::Math
