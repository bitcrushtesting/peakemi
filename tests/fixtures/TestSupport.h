#pragma once

#include <peakemi/core/Error.h>

#include <QByteArray>

#include <expected>

namespace peakemi::test {

/// Failure text for QVERIFY2, safe to evaluate on a successful result.
///
/// QVERIFY2 evaluates its description argument even when the condition holds,
/// so writing `result.error().message().c_str()` there is undefined behaviour
/// twice over: it reads the error of a result that may hold a value, and it
/// hands the macro a pointer into a std::string temporary that dies at the end
/// of the full expression. Keep the returned QByteArray in a local instead:
///
///     const auto reason = peakemi::test::errorText(parsed);
///     QVERIFY2(parsed.has_value(), reason.constData());
template<class T>
[[nodiscard]] inline QByteArray errorText(const std::expected<T, Error>& result)
{
    return result.has_value() ? QByteArray{} : QByteArray::fromStdString(result.error().message());
}

} // namespace peakemi::test
