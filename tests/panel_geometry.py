"""Shared physical geometry for generated Rack panel tests."""


def centered_stroke_outer_radius(radius_mm, stroke_width_mm):
    return float(radius_mm) + float(stroke_width_mm) / 2.0


def visible_output_material(radius_mm, stroke_width_mm,
                            obscuring_radius_mm):
    return (
        centered_stroke_outer_radius(radius_mm, stroke_width_mm)
        - float(obscuring_radius_mm)
    )
