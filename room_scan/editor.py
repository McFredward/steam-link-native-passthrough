"""Controller-driven semantic surface construction from measured world points.

Three corners define a gravity-aligned rectangle. Controller poses and scan
ray hits must be resolved into the same current origin before using this API.
"""
from __future__ import annotations

import math
import uuid

from .geometry import Pose, dot, number, require, sub, vector
from .model import Element, Geometry, Label, identifier


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def quaternion(x, y, z):
    """Quaternion for an orthonormal column basis, without an Euler convention."""
    m = ((x[0], y[0], z[0]), (x[1], y[1], z[1]), (x[2], y[2], z[2]))
    trace = m[0][0]+m[1][1]+m[2][2]
    if trace > 0:
        s = math.sqrt(trace+1)*2
        q = ((m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, s/4)
    else:
        i = max(range(3), key=lambda a: m[a][a])
        j, k = (i+1) % 3, (i+2) % 3
        s = math.sqrt(1+m[i][i]-m[j][j]-m[k][k])*2
        values = [0., 0., 0., 0.]
        values[i], values[j], values[k] = s/4, (m[i][j]+m[j][i])/s, (m[i][k]+m[k][i])/s
        values[3] = (m[k][j]-m[j][k])/s
        q = tuple(values)
    length = math.sqrt(dot(q, q))
    return tuple(a/length for a in q)


class SurfaceDraft:
    def __init__(self, label: Label, origin_id: str, *, parent_wall=None, element_id=None):
        require(label in (Label.FLOOR, Label.CEILING, Label.WALL_FACE, Label.DOOR_FRAME, Label.WINDOW_FRAME, Label.TABLE),
                "This editor creates surfaces")
        identifier(origin_id)
        self.label, self.origin_id, self.parent_wall = label, origin_id, parent_wall
        self.id = identifier(element_id) if element_id is not None else str(uuid.uuid4())
        self.points = []
        self.last_sample_ns = 0

    def add_corner(self, position, *, origin_id, sample_ns, tracking_valid):
        require(origin_id == self.origin_id and type(tracking_valid) is bool and tracking_valid,
                "Controller point belongs to unavailable/different tracking")
        require(type(sample_ns) is int and self.last_sample_ns < sample_ns < 2**63, "Stale controller sample")
        require(len(self.points) < 3, "Rectangle already has three corners")
        self.points.append(vector(position))
        self.last_sample_ns = sample_ns

    def undo_corner(self):
        require(bool(self.points), "No corner to undo")
        self.points.pop()

    def finish(self, name, *, furniture_depth_m=None):
        require(len(self.points) == 3, "Three measured corners are required")
        a, b, c = self.points
        horizontal = self.label in (Label.FLOOR, Label.CEILING, Label.TABLE)
        edge = sub(b, a)
        require(abs(edge[1]) <= .03, "First edge must be horizontal")
        width = math.hypot(edge[0], edge[2])
        require(.03 <= width <= 100, "Invalid surface width")
        x = (edge[0]/width, 0., edge[2]/width)
        y = cross((0., 1., 0.), x) if horizontal else (0., 1., 0.)
        height = dot(sub(c, a), y)
        if height < 0:
            y = tuple(-p for p in y)
            height = -height
        z = cross(x, y)
        require(.03 <= height <= 100 and abs(dot(sub(c, a), z)) <= .03, "Third corner lies outside the surface plane")
        require(abs(dot(sub(c, a), x)-width) <= .03, "Third point must be the opposite corner")
        center = tuple(a[i]+x[i]*width/2+y[i]*height/2 for i in range(3))
        pose = Pose(center, quaternion(x, y, z))
        boundary = ((-width/2, -height/2), (width/2, -height/2), (width/2, height/2), (-width/2, height/2))
        box_size = box_center = None
        if furniture_depth_m is not None:
            require(self.label == Label.TABLE and horizontal, "Only table surfaces accept a furniture depth")
            depth = number(furniture_depth_m, 100.)
            require(depth >= .01, "Invalid furniture depth")
            # The volume extends toward the floor irrespective of corner order.
            direction = -1 if z[1] > 0 else 1
            box_size, box_center = (width, height, depth), (0., 0., direction*depth/2)
        return Element(self.id, self.label, name, pose, Geometry(boundary, box_size, box_center),
                       parent_wall=self.parent_wall, confirmed=False, origin="manual")


def pick_surface(mesh, ray_origin, ray_direction, max_distance=6.):
    """Nearest measured triangle hit; gaps and unobserved space remain unpickable."""
    origin, direction = vector(ray_origin), vector(ray_direction, 3, 1.)
    require(abs(dot(direction, direction)-1) < 1e-5, "Ray direction must be normalized")
    require(.01 <= number(max_distance, 100.) <= 100., "Invalid ray distance")
    nearest = None
    for indices in mesh.triangles:
        # Uncertain geometry should guide another view, not silently snap an
        # authored table/window onto a weak reconstruction.
        if any(mesh.quality[i] != "reliable" for i in indices):
            continue
        a, b, c = (mesh.vertices[i] for i in indices)
        ab, ac = sub(b, a), sub(c, a)
        h = cross(direction, ac)
        det = dot(ab, h)
        if abs(det) < 1e-9:
            continue
        offset = sub(origin, a)
        u = dot(offset, h)/det
        q = cross(offset, ab)
        v = dot(direction, q)/det
        t = dot(ac, q)/det
        if u >= 0 and v >= 0 and u+v <= 1 and 0 < t <= max_distance and (nearest is None or t < nearest[0]):
            nearest = (t, tuple(origin[i]+direction[i]*t for i in range(3)))
    return nearest
