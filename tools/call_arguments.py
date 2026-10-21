# SPDX-License-Identifier: GPL-2.0-or-later
"""Bound scripted calls to six GP registers and ten raw overflow qwords."""

MAX_GP_ARGUMENTS = 16


class ArgumentError(ValueError):
    pass


def required_arguments(entry: dict) -> int:
    """Validate selectors and return the number of GP slots needed by a call script.

    argc controls the default trace selection. Other selectors may inspect arguments
    omitted from that trace; floating point registers have a separate index space.
    """
    if not isinstance(entry, dict):
        raise ArgumentError('call script must be an object')

    def bounded(value: object, field: str, limit: int) -> int:
        if type(value) is not int or not 0 <= value <= limit:
            raise ArgumentError(f'{field} must be an integer in 0..{limit}')
        return value

    def sequence(field: str) -> list:
        values = entry.get(field, [])
        if not isinstance(values, list):
            raise ArgumentError(f'{field} must be a list')
        return values

    count = bounded(entry.get('argc', 0), 'argc', MAX_GP_ARGUMENTS)
    for index in sequence('argi'):
        count = max(count, bounded(index, 'argi', MAX_GP_ARGUMENTS - 1) + 1)
    for field in ('argmem', 'writes'):
        for item in sequence(field):
            if not isinstance(item, dict):
                raise ArgumentError(f'{field} entry must be an object')
            count = max(count, bounded(item.get('arg'), f'{field}.arg', MAX_GP_ARGUMENTS - 1) + 1)
    if 'series' in entry:
        series = entry['series']
        if not isinstance(series, dict):
            raise ArgumentError('series must be an object')
        count = max(count, bounded(series.get('arg'), 'series.arg', MAX_GP_ARGUMENTS - 1) + 1)
    for index in sequence('argf32'):
        bounded(index, 'argf32', 1)
    return count
