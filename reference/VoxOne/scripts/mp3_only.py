Import("env")


def mp3_only(env, node):
    path = node.srcnode().get_abspath().replace("\\", "/")
    if "/libhelix/src/" in path and "/libhelix/src/libhelix-mp3/" not in path:
        return None
    return node


env.AddBuildMiddleware(mp3_only)
