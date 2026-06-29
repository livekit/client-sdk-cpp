# TokenSource Factory Shape Options

This note summarizes possible public API shapes for constructing TokenSource
implementations in the C++ SDK.

## 1. Previous API

Concrete classes exposed mechanism-specific static factories:

```cpp
LiteralTokenSource::fromLiteral(url, token);
LiteralTokenSource::fromProvider(provider);
CustomTokenSource::fromCustom(callback);
EndpointTokenSource::fromEndpoint(url, options);
SandboxTokenSource::fromSandboxTokenServer(sandbox_id, options);
CachingTokenSource::wrap(std::move(source));
```

Pros:

- Matches Android/JS naming concepts fairly closely.
- Explicit at the call site.
- Preserves the current `std::unique_ptr` ownership model.

Cons:

- Verbose and repetitive because the class name already says the mechanism.

Example:

```cpp
auto source = SandboxTokenSource::fromSandboxTokenServer("sandbox-id");
```

## 2. Per-Class `create()`

Keep concrete classes, but standardize factory names:

```cpp
LiteralTokenSource::create(url, token);
CustomTokenSource::create(callback);
EndpointTokenSource::create(url, options);
SandboxTokenSource::create(sandbox_id, options);
CachingTokenSource::create(std::move(source));
```

Pros:

- More idiomatic with this SDK's existing `AudioFrame::create` /
  `VideoFrame::create` style.
- Concise while keeping each concrete source discoverable.
- Keeps the current factory ownership model.

Cons:

- Less directly aligned with Android/JS names.
- `LiteralTokenSource::fromProvider` may still need a distinct name or a very
  clear overload.

Example:

```cpp
auto source = SandboxTokenSource::create("sandbox-id");
```

## 3. Root `TokenSource` Factory

Add a root factory facade, similar to Android/JS:

```cpp
TokenSource::fromLiteral(url, token);
TokenSource::fromCustom(callback);
TokenSource::fromEndpoint(url, options);
TokenSource::fromSandboxTokenServer(sandbox_id, options);
```

Alternative JS-style naming:

```cpp
TokenSource::literal(...);
TokenSource::custom(...);
TokenSource::endpoint(...);
TokenSource::sandboxTokenServer(...);
```

Pros:

- Best cross-SDK discoverability.
- Groups all TokenSource creation in one place.
- Avoids repeating mechanism names on concrete classes.

Cons:

- `TokenSource` would be a factory facade, not the polymorphic interface.
- May confuse the distinction between `TokenSourceFixed` and
  `TokenSourceConfigurable`.

Example:

```cpp
auto source = TokenSource::fromSandboxTokenServer("sandbox-id");
```

## 4. Constructors

Make concrete sources directly constructible. This is the current branch shape:

```cpp
LiteralTokenSource source(url, token);
CustomTokenSource source(callback);
EndpointTokenSource source(url, options);
SandboxTokenSource source(sandbox_id, options);
CachingTokenSource source(std::move(inner));
```

Pros:

- Most C++-native for concrete types.
- Shortest syntax for stack usage.

Cons:

- Caching still requires an owned configurable source, typically via
  `std::make_unique`.

Example:

```cpp
SandboxTokenSource source("sandbox-id");
auto credentials = source.fetch(request).get();
```
